#!/usr/bin/env python3
"""Standalone offline map processing; cached detections, never cached map outputs."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import signal
import shutil
import subprocess
import sys
import time

os.environ.setdefault('MPLCONFIGDIR', '/tmp/agt-studio-algorithms-mpl')
import numpy as np
from scipy import ndimage as ndi
from scipy.spatial import cKDTree
from scipy.spatial.transform import Rotation
import yaml

HERE = Path(__file__).resolve().parent
CHILD = None


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, HERE / filename)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def command(arguments):
    global CHILD
    CHILD = subprocess.Popen([str(x) for x in arguments])
    try:
        code = CHILD.wait()
    finally:
        CHILD = None
    if code:
        raise RuntimeError(f'Algorithm process exited with {code}')


def cancel(signum, frame):
    if CHILD is not None:
        CHILD.terminate()
        try:
            CHILD.wait(timeout=1)
        except subprocess.TimeoutExpired:
            CHILD.kill()
    raise SystemExit(130)


def progress(message):
    print(message, flush=True)


def write_pcd(path, fields, values):
    values = np.asarray(values, dtype='<f4')
    n, columns = values.shape
    header = (f'# AGT Studio offline result\nVERSION .7\nFIELDS {" ".join(fields)}\n'
              f'SIZE {" ".join(["4"]*columns)}\nTYPE {" ".join(["F"]*columns)}\n'
              f'COUNT {" ".join(["1"]*columns)}\nWIDTH {n}\nHEIGHT 1\n'
              f'VIEWPOINT 0 0 0 1 0 0 0\nPOINTS {n}\nDATA binary\n')
    with Path(path).open('wb') as stream:
        stream.write(header.encode('ascii'))
        stream.write(values.tobytes())


def self_mask(io, values, fields, package):
    columns = {k:i for i,k in enumerate(fields)}
    xyz = values[:, [columns[k] for k in ('x','y','z')]].astype(float)
    tree = cKDTree(xyz)
    inside = np.zeros(len(xyz), np.uint16)
    outside = np.zeros(len(xyz), np.uint16)
    for number, row in enumerate((package/'poses_timed.txt').read_text().splitlines()):
        parts = row.split()
        p = np.asarray(parts[2:9], float)
        _, names, scan = io.read_pcd(package/'patches'/parts[0])
        body = scan[:, [names.index(k) for k in ('x','y','z')]].astype(float)
        world = Rotation.from_quat(p[[4,5,6,3]]).apply(body) + p[:3]
        region = ((body[:,0]>=-.85)&(body[:,0]<=-.4)&
                  (body[:,1]>=-.2)&(body[:,1]<=.2)&
                  (body[:,2]>=-.2)&(body[:,2]<=1.))
        groups = tree.query_ball_point(world[region], .04)
        ids = np.unique([i for group in groups for i in group]).astype(int)
        inside[ids] += 1
        distances, ids = tree.query(world[~region])
        outside[np.unique(ids[distances<=.04])] += 1
        if (number+1)%60 == 0:
            progress(f'Vehicle body filter: checked {number+1} frames')
    parked = values[:,columns['parked_car_mask']]>.5
    return (inside>0)&(outside<3)&~parked


def radius_mask(xyz, candidates, vehicle, fields, radius, minimum):
    ids = np.flatnonzero(candidates)
    rejected = np.zeros(len(xyz), bool)
    if len(ids):
        distances = cKDTree(xyz[ids,:2]).query(xyz[ids,:2], k=[minimum],
                        distance_upper_bound=np.nextafter(radius, np.inf), workers=-1)[0][:,0]
        protected = vehicle[ids] | (fields['parked_car_mask'][ids]>.5)
        rejected[ids[~np.isfinite(distances)&~protected]] = True
    return rejected


def fuse_free(before, valid, slope, ground_hits, clearance_free, clearance_occupied, support, resolution, frames):
    distance = ndi.distance_transform_edt(ground_hits==0)*resolution
    new = ((before==205)&valid&(slope<=20)&(distance<=.3)&
           (clearance_free==8)&(clearance_occupied==0)&(support>=frames))
    after = before.copy()
    after[new] = 254
    return after, new


def validate(profile, package):
    paths = [package/name for name in ('map.pcd', 'poses.txt', 'poses_timed.txt', 'metadata.yaml')]
    paths.extend(Path(value) for value in profile['assets'].values())
    for path in paths:
        if not path.is_file():
            raise ValueError(f'Required input file is missing: {path}')
    if not (package/'patches').is_dir() or not any((package/'patches').glob('*.pcd')):
        raise ValueError(f'No keyframe PCD files found: {package / "patches"}')


def load_profile(filename):
    """Resolve asset paths relative to data configuration, independent of CWD."""
    filename = Path(filename).resolve()
    profile = json.loads(filename.read_text())
    def resolve(value):
        return str((filename.parent / value).resolve())
    profile['package'] = resolve(profile['package'])
    profile['assets'] = {key: resolve(value) for key, value in profile['assets'].items()}
    # Data configuration never selects algorithms or supplies processing defaults.
    profile = {key: profile[key] for key in ('schema_version', 'name', 'package', 'assets', 'coordinate_system') if key in profile}
    required = {'detections', 'source', 'baseline', 'parked', 'evidence', 'tracks'}
    if not required.issubset(profile['assets']):
        raise ValueError('Processing profile is missing required detection or map assets')
    return profile


def package_result(output, package, point_count):
    """Keep the clean map and its pose provenance usable by the existing workflow."""
    for name in ('poses.txt','poses_timed.txt','calibration.yaml'):
        if (package/name).is_file():
            shutil.copy2(package/name, output/name)
    shutil.copytree(package/'patches',output/'patches')
    metadata = yaml.safe_load((package/'metadata.yaml').read_text())
    metadata['processing_output_points'] = point_count
    metadata['parent_package'] = os.path.relpath(package, output)
    (output/'metadata.yaml').write_text(yaml.safe_dump(metadata,sort_keys=False))
    files = [output/'map.pcd',output/'metadata.yaml',output/'poses.txt',output/'poses_timed.txt',
             *sorted((output/'patches').glob('*.pcd'))]
    if (output/'calibration.yaml').is_file():
        files.append(output/'calibration.yaml')
    checksums = {p.relative_to(output).as_posix():sha(p) for p in files}
    (output/'checksums.sha256').write_text(''.join(f'{v}  {k}\n' for k,v in checksums.items()))
    manifest = dict(schema_version=1,package_kind='processed_mapping_source',
                    parent_package=os.path.relpath(package, output), parent_map_sha256=sha(package/'map.pcd'),
                    checksums_file='checksums.sha256',checksums=checksums,
                    patch_semantics='Original scans retained as observation provenance; map.pcd is filtered.')
    (output/'manifest.yaml').write_text(yaml.safe_dump(manifest,sort_keys=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, help='Map data processing_profile.json')
    parser.add_argument('--package', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--octomap-builder', type=Path, default=Path(sys.argv[0]).absolute().parent/'octomap_builder')
    parser.set_defaults(centerpoint=1,self_filter=1,radius_filter=1,octomap=1,
                        radius=.2,neighbors=5,frames=3)
    parser.add_argument('--radius', type=float, default=.2, help='Obstacle neighbor radius in meters (default: 0.2)')
    parser.add_argument('--neighbors', type=int, default=5, help='Minimum neighbors including self (default: 5)')
    parser.add_argument('--frames', type=int, default=3, help='Minimum independent free observations per layer (default: 3)')
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    if not 0<args.radius<=5 or not 1<=args.neighbors<=100 or not 1<=args.frames<=255:
        parser.error('radius / neighbors / frames out of range')
    if args.profile is None:
        if args.package is None:
            parser.error('Provide --package (with processing_profile.json) or --profile')
        args.profile = args.package/'processing_profile.json'
    profile = load_profile(args.profile)
    package = (args.package or Path(profile['package'])).resolve()
    manifest = package/'manifest.yaml'
    if manifest.is_file():
        kind = (yaml.safe_load(manifest.read_text()) or {}).get('package_kind')
        if kind in ('processed_mapping_source', 'studio_algorithm_mapping_source'):
            # Repeat processing from the original observations, not a filtered map.
            package = Path(profile['package'])
    progress('Checking map, poses, keyframe and detection asset files...')
    validate(profile, package)
    if args.octomap and (not args.octomap_builder or not os.access(args.octomap_builder, os.X_OK)):
        raise ValueError('OctoMap builder is unavailable. Build agt_map_processing or pass --octomap-builder.')
    if args.check_only:
        progress('Input validated. CenterPoint reuses cached detections and recomputes tracking and point labels.')
        return
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    start = time.monotonic()
    asset = {k:Path(v) for k,v in profile['assets'].items()}
    io = module('studio_cp', 'centerpoint_postprocess.py')
    cp_output = output/'centerpoint'
    if args.centerpoint:
        progress('1/4 CenterPoint: cached detections -> tracking -> measured-return point labels')
        command([sys.executable, HERE/'centerpoint_postprocess.py','--output-dir',cp_output,
                 '--config', HERE/'centerpoint_config.json','--package',package,
                 *[argument for key in ('detections','source','baseline','parked','evidence')
                   for argument in (f'--{key}',asset[key])]])
        source = cp_output/'map_centerpoint_people_cars_confidence.pcd'
    else:
        # Reviewed parking/legacy evidence still belong to the common base.
        source = asset['baseline']
    _, fields, values = io.read_pcd(source)
    columns = {k:i for i,k in enumerate(fields)}
    if 'parked_car_mask' not in columns:
        fields = fields + ['parked_car_mask']
        values = np.column_stack([values,values[:,columns['vehicle_box_mask']]])
        columns = {k:i for i,k in enumerate(fields)}
    removed_self = np.zeros(len(values), bool)
    if args.self_filter:
        progress('2/4 Vehicle body filter: per-frame BODY/IMU box with external-frame and parked-vehicle protection')
        removed_self = self_mask(io, values, fields, package)
    # Full annotation retains original geometry and provenance for audit.
    annotated = output/'annotated.pcd'
    write_pcd(annotated, fields+['self_vehicle_mask'], np.column_stack([values, removed_self]))
    grid = module('studio_grid','ground_grid.py')
    grid.SOURCE = annotated
    grid.OUT = output/'ground_navigation'
    grid.POSES = package/'poses.txt'
    grid.POSES_TIMED = package/'poses_timed.txt'
    grid.DETECTIONS = asset['tracks']
    read = grid.read_pcd
    def read_masks(path):
        data = read(path)
        extra = data['self_vehicle_mask']>.5
        if args.centerpoint:
            extra |= (data['pedestrian_mask']>.5)&(data['centerpoint_motion_mask']>.5)
        data['dynamic_override'] = np.maximum(data['dynamic_override'], extra)
        return data
    grid.read_pcd = read_masks
    rejected_radius = np.zeros(len(values), bool)
    def filter_candidates(xyz, candidates, vehicle, data):
        nonlocal rejected_radius
        rejected_radius = radius_mask(xyz,candidates,vehicle,data,args.radius,args.neighbors)
        return candidates&~rejected_radius
    if args.radius_filter:
        grid.OBSTACLE_FILTER = filter_candidates
    progress('3/4 Ground separation, radius filtering of obstacle candidates, and 2D grid projection')
    grid.main()
    removed = grid.POINT_DYNAMIC | rejected_radius
    xyzi = values[:, [fields.index(k) for k in ('x','y','z','intensity')]]
    write_pcd(output/'map.pcd', ['x','y','z','intensity'], xyzi[~removed])
    write_pcd(output/'removed.pcd', ['x','y','z','intensity'], xyzi[removed])
    reason = (grid.POINT_DYNAMIC.astype(np.uint8) + removed_self.astype(np.uint8)*2 + rejected_radius.astype(np.uint8)*4)
    np.savez_compressed(output/'point_masks.npz', removed=removed, reason_bits=reason,
                        self_vehicle=removed_self, radius_rejected=rejected_radius)
    layers = np.load(grid.OUT/'layers.npz')
    before = layers['geometry_occupancy']
    after = before.copy()
    new = np.zeros(before.shape, bool)
    navigation = output/'navigation'
    navigation.mkdir()
    if args.octomap:
        progress('4/4 OctoMap: reinserting optimized keyframes and checking eight clearance layers with independent-frame support')
        octo = output/'octomap'
        octo.mkdir()
        layers['ground_height'].astype('<f4').tofile(octo/'ground.f32')
        layers['ground_model_valid'].astype('u1').tofile(octo/'valid.u8')
        before.tofile(octo/'before.u8')
        origin = layers['origin']
        resolution = float(layers['resolution'])
        command([args.octomap_builder,package/'poses_timed.txt',package/'patches',
                 octo/'ground.f32',octo/'valid.u8',octo/'before.u8',before.shape[1],before.shape[0],
                 origin[0],origin[1],resolution,octo,.2, str(args.self_filter)])
        array = lambda name: np.fromfile(octo/name,np.uint8).reshape(before.shape)
        after,new = fuse_free(before,layers['ground_model_valid'],layers['slope_degrees'],
                              layers['ground_hits'],array('clearance_free.u8'),
                              array('clearance_occupied.u8'),array('clearance_min_frame_support.u8'),
                              resolution,args.frames)
    if not np.array_equal(after[before==0],before[before==0]):
        raise AssertionError('OctoMap changed occupied cells')
    with (navigation/'map.pgm').open('wb') as stream:
        stream.write(f'P5\n{after.shape[1]} {after.shape[0]}\n255\n'.encode())
        stream.write(np.flipud(after).tobytes())
    metadata = dict(image='map.pgm',mode='trinary',resolution=float(layers['resolution']),
                    origin=[*layers['origin'].tolist(),0.0],negate=0,occupied_thresh=.65,free_thresh=.196)
    (navigation/'map.yaml').write_text(yaml.safe_dump(metadata,sort_keys=False))
    np.savez_compressed(navigation/'layers.npz',before=before,occupancy=after,
                        origin=layers['origin'],resolution=layers['resolution'])
    package_result(output,package,int((~removed).sum()))
    validate(profile, package)
    report = dict(status='complete', package=str(package), profile=str(args.profile.resolve()),
        algorithms={k:getattr(args,k) for k in ('centerpoint','self_filter','radius_filter','octomap')},
        centerpoint_mode='cached_detections_recompute_tracking_and_point_masks' if args.centerpoint else 'disabled',
        legacy_motion_evidence=True, self_box_frame='BODY/IMU',
        radius_m=args.radius, neighbors_including_self=args.neighbors, distinct_frames=args.frames,
        input_points=len(values), output_points=int((~removed).sum()), removed_points=int(removed.sum()),
        self_removed_points=int(removed_self.sum()), radius_removed_points=int(rejected_radius.sum()),
        new_free_cells=int(new.sum()), grid_counts={str(v):int((after==v).sum()) for v in (0,205,254)},
        input_validation='file_presence_and_runtime_format', input_sha256_checked=False, seconds=round(time.monotonic()-start,2),
        profile_sha256=sha(args.profile),
        implementation_sha256={p.name:sha(p) for p in HERE.glob('*.py')},
        outputs=dict(pcd=str(output/'map.pcd'), map_yaml=str(navigation/'map.yaml')),
        notes=['OctoMap supplements unknown→free; it does not delete map PCD points.',
               'OctoMap ray evidence uses original optimized scans; removed points are not extended as free rays.',
               'PCD removes the same dynamic/self/radius points excluded from grid obstacle evidence.',
               'The legacy motion evidence and ground rules remain part of this replay profile.'])
    from prepare_map import prepare
    prepare(args.profile, output/'processing_profile.json')
    (output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    progress(f'Completed: {report["output_points"]} cloud points; OctoMap added {report["new_free_cells"]} free cells.\nOutput: {output}')


if __name__=='__main__':
    signal.signal(signal.SIGTERM,cancel)
    signal.signal(signal.SIGINT,cancel)
    try:
        main()
    except Exception as error:
        print(f'Processing failed: {error}',file=sys.stderr,flush=True)
        sys.exit(1)
