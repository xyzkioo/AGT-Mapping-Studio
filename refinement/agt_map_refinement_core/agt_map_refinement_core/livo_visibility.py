"""Optional, offline FAST-LIVO map cleanup using upstream visibility evidence.

The visibility vote comes from rsasaki0109/dynamic-3d-object-removal (MIT).
The route and floor guards below restrict where that vote may alter a map.
"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

import numpy as np
from scipy.spatial import cKDTree

from .opensource_visibility import clean_map_by_visibility

UPSTREAM_URL = 'https://github.com/rsasaki0109/dynamic-3d-object-removal'
UPSTREAM_COMMIT = '0a640ba198a00d554a8538a0c7a1d955d6d85356'


def read_binary_pcd(path: Path) -> tuple[np.ndarray, list[str], list[str]]:
    with path.open('rb') as stream:
        header = []
        while True:
            line = stream.readline()
            if not line:
                raise ValueError(f'PCD has no DATA line: {path}')
            decoded = line.decode('ascii').rstrip('\r\n')
            header.append(decoded)
            if decoded.upper().startswith('DATA '):
                break
        values = {line.split()[0].upper(): line.split()[1:]
                  for line in header if line and not line.startswith('#')}
        fields = values['FIELDS']
        if (values['DATA'] != ['binary'] or
                values['SIZE'] != ['4'] * len(fields) or
                values['TYPE'] != ['F'] * len(fields) or
                values.get('COUNT', ['1'] * len(fields)) != ['1'] * len(fields) or
                not {'x', 'y', 'z'}.issubset(fields)):
            raise ValueError('Expected an uncompressed binary PCD with float32 x/y/z fields')
        count = int(values['POINTS'][0])
        points = np.frombuffer(stream.read(), dtype='<f4')
    if points.size != count * len(fields):
        raise ValueError('PCD payload size differs from POINTS/FIELDS')
    return points.reshape(count, len(fields)).copy(), header, fields


def write_binary_pcd(path: Path, points: np.ndarray, header: list[str]) -> None:
    rewritten = []
    for line in header:
        key = line.split(maxsplit=1)[0].upper() if line.split() else ''
        if key in ('WIDTH', 'POINTS'):
            line = f'{key} {len(points)}'
        elif key == 'HEIGHT':
            line = 'HEIGHT 1'
        rewritten.append(line)
    with path.open('wb') as stream:
        stream.write(('\n'.join(rewritten) + '\n').encode('ascii'))
        stream.write(np.ascontiguousarray(points, dtype='<f4').tobytes())


def read_poses(path: Path) -> tuple[np.ndarray, np.ndarray]:
    with path.open(newline='') as stream:
        reader = csv.DictReader(stream)
        if not {'stamp_ns', 'x', 'y', 'z'}.issubset(reader.fieldnames or []):
            raise ValueError('Pose CSV needs stamp_ns,x,y,z columns')
        rows = [(int(row['stamp_ns']), float(row['x']), float(row['y']),
                 float(row['z'])) for row in reader]
    if not rows:
        raise ValueError('Pose CSV is empty')
    poses = np.asarray(rows, dtype=np.float64)
    order = np.argsort(poses[:, 0])
    poses = poses[order]
    stamps = np.asarray([rows[i][0] for i in order], dtype=np.int64)
    return stamps, poses[:, 1:4]


def _closest_pose(stamps: np.ndarray, stamp: int) -> tuple[int, int]:
    i = int(np.searchsorted(stamps, stamp))
    candidates = [k for k in (i - 1, i) if 0 <= k < len(stamps)]
    k = min(candidates, key=lambda index: abs(int(stamps[index]) - stamp))
    return k, abs(int(stamps[k]) - stamp)


def read_registered_scans(bag: Path, topic: str, stamps: np.ndarray,
                          origins: np.ndarray, region: tuple[float, ...],
                          stride: int, max_pose_gap_s: float):
    # ROS imports stay here, so --help and package import work without a sourced ROS shell.
    import rosbag2_py
    from rclpy.serialization import deserialize_message
    from sensor_msgs.msg import PointCloud2, PointField

    reader = rosbag2_py.SequentialReader()
    reader.open(rosbag2_py.StorageOptions(uri=str(bag), storage_id='sqlite3'),
                rosbag2_py.ConverterOptions(input_serialization_format='cdr',
                                            output_serialization_format='cdr'))
    topic_types = {item.name: item.type for item in reader.get_all_topics_and_types()}
    if topic_types.get(topic) != 'sensor_msgs/msg/PointCloud2':
        raise ValueError(f'{topic} is not a PointCloud2 topic in {bag}')
    reader.set_filter(rosbag2_py.StorageFilter(topics=[topic]))
    scans = []
    seen = 0
    skipped_time = 0
    while reader.has_next():
        _, payload, _ = reader.read_next()
        seen += 1
        if seen % stride:
            continue
        msg = deserialize_message(payload, PointCloud2)
        stamp = msg.header.stamp.sec * 1_000_000_000 + msg.header.stamp.nanosec
        pose_index, gap = _closest_pose(stamps, stamp)
        if gap > max_pose_gap_s * 1e9:
            skipped_time += 1
            continue
        origin = origins[pose_index]
        x0, x1, y0, y1 = region
        if not (x0 <= origin[0] <= x1 and y0 <= origin[1] <= y1):
            continue
        field_map = {field.name: field for field in msg.fields}
        if not {'x', 'y', 'z'}.issubset(field_map):
            raise ValueError('Registered PointCloud2 lacks x/y/z')
        if any(field_map[name].datatype != PointField.FLOAT32
               for name in ('x', 'y', 'z')) or msg.is_bigendian:
            raise ValueError('Registered PointCloud2 must have little-endian float32 x/y/z')
        dtype = np.dtype({'names': ['x', 'y', 'z'],
                          'formats': ['<f4'] * 3,
                          'offsets': [field_map[name].offset for name in ('x', 'y', 'z')],
                          'itemsize': msg.point_step})
        cloud = np.ndarray((msg.height, msg.width), dtype=dtype, buffer=msg.data,
                           strides=(msg.row_step, msg.point_step))
        xyz = np.column_stack([cloud[name].reshape(-1) for name in ('x', 'y', 'z')])
        xyz = xyz[np.isfinite(xyz).all(axis=1)]
        if len(xyz):
            scans.append((xyz, origin.copy()))
    return scans, {'registered_messages': seen, 'selected_scans': len(scans),
                   'skipped_pose_time_gap': skipped_time}


def clean(args: argparse.Namespace) -> dict:
    source = args.map_pcd.resolve()
    output = args.output_dir.resolve()
    cleaned_file = output / 'localization_map_cleaned.pcd'
    if source == cleaned_file or source == output / 'removed_points.pcd':
        raise ValueError('Output would overwrite the source map')
    if any((output / name).exists() for name in
           ('localization_map_cleaned.pcd', 'removed_points.pcd', 'report.json')):
        raise FileExistsError(f'Output files already exist in {output}; choose a new directory')
    points, header, fields = read_binary_pcd(source)
    xyz = points[:, [fields.index(axis) for axis in ('x', 'y', 'z')]]
    stamps, origins = read_poses(args.poses_csv)
    x0, x1, y0, y1 = args.region_xy
    if x0 >= x1 or y0 >= y1 or args.scan_stride < 1:
        raise ValueError('Invalid region or scan stride')
    if args.path_radius <= 0 or args.ground_cell <= 0:
        raise ValueError('Path radius and ground cell must be positive')
    if args.min_height >= args.max_height:
        raise ValueError('Minimum height must be below maximum height')
    region = ((xyz[:, 0] >= x0) & (xyz[:, 0] <= x1) &
              (xyz[:, 1] >= y0) & (xyz[:, 1] <= y1) &
              (xyz[:, 2] >= args.min_z) & (xyz[:, 2] <= args.max_z))
    indices = np.flatnonzero(region)
    if not len(indices):
        raise ValueError('Region contains no map points')
    subset = xyz[indices]
    scans, scan_report = read_registered_scans(args.bag, args.registered_topic,
                                               stamps, origins, args.region_xy,
                                               args.scan_stride, args.max_pose_gap)
    if not scans:
        raise ValueError('No aligned registered scans in the selected region')
    _, upstream_keep = clean_map_by_visibility(
        subset, scans, h_res_deg=args.angular_resolution,
        v_res_deg=args.angular_resolution, range_margin=args.range_margin,
        min_see_through=args.min_see_through,
        max_surface_hits=args.max_surface_hits, ground_z=args.ground_z)

    # The upstream vote proposes deletions; these independent guards limit their reach.
    path_distance = cKDTree(origins[:, :2]).query(subset[:, :2], workers=-1)[0]
    cells = np.floor(subset[:, :2] / args.ground_cell).astype(np.int32)
    _, inverse = np.unique(cells, axis=0, return_inverse=True)
    floor = np.full(int(inverse.max()) + 1, np.inf, dtype=np.float32)
    np.minimum.at(floor, inverse, subset[:, 2])
    above_floor = subset[:, 2] - floor[inverse]
    remove_local = ((~upstream_keep) & (path_distance <= args.path_radius) &
                    (above_floor >= args.min_height) &
                    (above_floor <= args.max_height))
    remove = np.zeros(len(points), dtype=bool)
    remove[indices] = remove_local
    output.mkdir(parents=True, exist_ok=True)
    write_binary_pcd(cleaned_file, points[~remove], header)
    write_binary_pcd(output / 'removed_points.pcd', points[remove], header)
    report = {
        'source_map': str(source), 'cleaned_map': str(cleaned_file),
        'source_points': len(points), 'region_points': len(indices),
        'upstream_candidate_points': int((~upstream_keep).sum()),
        'guarded_removed_points': int(remove.sum()),
        'retained_points': int((~remove).sum()),
        'upstream': {'url': UPSTREAM_URL, 'commit': UPSTREAM_COMMIT,
                     'method': 'clean_map_by_visibility'},
        'scan': scan_report,
        'parameters': {name: getattr(args, name) for name in (
            'registered_topic', 'scan_stride', 'max_pose_gap', 'region_xy',
            'min_z', 'max_z', 'angular_resolution', 'range_margin',
            'min_see_through', 'max_surface_hits', 'ground_z', 'path_radius',
            'ground_cell', 'min_height', 'max_height')},
        'review_only': True,
    }
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--map-pcd', required=True, type=Path)
    parser.add_argument('--bag', required=True, type=Path)
    parser.add_argument('--poses-csv', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--registered-topic', default='/agt/commissioning/mapping/registered_points')
    parser.add_argument('--region-xy', required=True, type=float, nargs=4,
                        metavar=('X_MIN', 'X_MAX', 'Y_MIN', 'Y_MAX'))
    parser.add_argument('--min-z', type=float, default=-2.5)
    parser.add_argument('--max-z', type=float, default=2.0)
    parser.add_argument('--scan-stride', type=int, default=40)
    parser.add_argument('--max-pose-gap', type=float, default=0.25)
    parser.add_argument('--angular-resolution', type=float, default=2.5)
    parser.add_argument('--range-margin', type=float, default=0.5)
    parser.add_argument('--min-see-through', type=int, default=4)
    parser.add_argument('--max-surface-hits', type=int, default=2)
    parser.add_argument('--ground-z', type=float, default=-1.2)
    parser.add_argument('--path-radius', type=float, default=1.5)
    parser.add_argument('--ground-cell', type=float, default=0.5)
    parser.add_argument('--min-height', type=float, default=1.0)
    parser.add_argument('--max-height', type=float, default=2.1)
    args = parser.parse_args()
    print(json.dumps(clean(args), indent=2))


if __name__ == '__main__':
    main()
