#!/usr/bin/env python3
"""Offline ground-relative occupancy layers from the staged confidence PCD."""
from pathlib import Path
import hashlib
import json
import time

import numpy as np
from scipy import ndimage as ndi
from scipy.spatial.transform import Rotation, Slerp
import yaml
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap, LinearSegmentedColormap
from matplotlib.patches import Rectangle
from PIL import Image

OUT = Path(__file__).resolve().parent
SOURCE = OUT.parent / 'map_scheme1_staged_confidence.pcd'
TRIAL_ROOT = OUT.parents[2]
DETECTIONS = TRIAL_ROOT / 'centerpoint_tracks.json'
POSES = OUT / 'poses.txt'
POSES_TIMED = POSES.with_name('poses_timed.txt')
RES = 0.1
COARSE = 0.5
PARAMS = dict(resolution_m=RES, ground_grid_m=COARSE, ground_tolerance_m=0.14,
              obstacle_min_height_m=0.22, obstacle_max_height_m=2.2,
              minimum_obstacle_points=2, motion_exclusion_threshold=0.75,
              maximum_slope_degrees=20, maximum_step_m=0.22,
              interpolated_free_gap_max_m=0.2, obstacle_bridge_radius_m=0.1)
PARAMS.update(trajectory_front_m=.4, trajectory_rear_m=.72, trajectory_half_width_m=.46)
PARAMS.update(minimum_car_track_speed_mps=.6, minimum_car_track_displacement_m=.5)
APPLY_LOCAL_CAR_FILTER = False
OBSTACLE_FILTER = None
POINT_DYNAMIC = None


def read_pcd(path):
    with path.open('rb') as f:
        header = {}
        while True:
            raw_line = f.readline()
            if not raw_line:
                raise ValueError('PCD header missing DATA')
            line = raw_line.decode('ascii').strip()
            if not line:
                continue
            parts = line.split()
            header[parts[0]] = parts[1:]
            if parts[0] == 'DATA':
                break
        assert header['DATA'] == ['binary']
        fields = header['FIELDS']
        assert header['SIZE'] == ['4'] * len(fields)
        assert header['TYPE'] == ['F'] * len(fields)
        assert header['COUNT'] == ['1'] * len(fields)
        values = np.frombuffer(f.read(), dtype='<f4').reshape(-1, len(fields))
        assert len(values) == int(header['POINTS'][0])
    return {k: values[:, i] for i, k in enumerate(fields)}


def counts(ids, mask, shape):
    return np.bincount(ids[mask], minlength=np.prod(shape)).reshape(shape)


def pgm(name, array):
    a = np.flipud(array).astype(np.uint8)
    with (OUT / name).open('wb') as f:
        f.write(f'P5\n{a.shape[1]} {a.shape[0]}\n255\n'.encode())
        f.write(a.tobytes())


def main():
    start = time.monotonic()
    OUT.mkdir(parents=True, exist_ok=True)
    checksum = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    d = read_pcd(SOURCE)
    xyz = np.column_stack([d['x'], d['y'], d['z']]).astype(np.float64)
    finite = np.isfinite(xyz).all(axis=1)
    dynamic = (d['dynamic_override'] > 0.5) | (d['motion_score'] >= PARAMS['motion_exclusion_threshold'])
    global POINT_DYNAMIC
    POINT_DYNAMIC = dynamic.copy()
    car = d['vehicle_box_mask'] > 0.5
    stable = finite & ~dynamic
    pose_data = np.array([[float(v) for v in line.split()[1:8]] for line in POSES.read_text().splitlines() if line.strip()])
    route = pose_data[:, :3]
    frame_times = {int(line.split()[0].split('.')[0]): float(line.split()[1])
                   for line in POSES_TIMED.read_text().splitlines() if line.strip()}
    origin = np.floor((xyz[finite, :2].min(axis=0) - 1) / RES) * RES
    dims = np.ceil((xyz[finite, :2].max(axis=0) + 1 - origin) / RES).astype(int) + 1
    shape = (int(dims[1]), int(dims[0]))
    ij = np.floor((xyz[:, :2] - origin) / RES).astype(int)
    ids = ij[:, 1] * shape[1] + ij[:, 0]
    coarse_shape = (int(np.ceil(shape[0] / 5)), int(np.ceil(shape[1] / 5)))
    cij = ij // 5
    cids = cij[:, 1] * coarse_shape[1] + cij[:, 0]

    # Low returns in each coarse cell supply plane candidates; RANSAC rejects
    # cars, walls, trees and isolated low outliers without flattening the PCD.
    seed_idx = np.flatnonzero(stable & ~car)
    order = seed_idx[np.argsort(cids[seed_idx], kind='stable')]
    groups, beginnings, numbers = np.unique(cids[order], return_index=True, return_counts=True)
    lower_xyz = []
    for cell, begin, number in zip(groups, beginnings, numbers):
        if number < 6:
            continue
        idx = order[begin:begin + number]
        lower_xyz.append([origin[0] + (cell % coarse_shape[1] + .5) * COARSE,
                          origin[1] + (cell // coarse_shape[1] + .5) * COARSE,
                          float(np.quantile(xyz[idx, 2], .12))])
    lower_xyz = np.asarray(lower_xyz)
    design = np.column_stack([lower_xyz[:, :2], np.ones(len(lower_xyz))])
    rng = np.random.default_rng(20260928)
    best_score = -1
    for _ in range(500):
        subset = rng.choice(len(lower_xyz), 3, replace=False)
        try:
            plane = np.linalg.solve(design[subset], lower_xyz[subset, 2])
        except np.linalg.LinAlgError:
            continue
        if np.linalg.norm(plane[:2]) > np.tan(np.deg2rad(20)):
            continue
        residual = np.abs(design @ plane - lower_xyz[:, 2])
        score = np.count_nonzero(residual < .18)
        if score > best_score:
            best_score, best_plane = score, plane
    if best_score < 100:
        raise RuntimeError('Insufficient consistent ground evidence')
    for _ in range(4):
        inlier = np.abs(design @ best_plane - lower_xyz[:, 2]) < .22
        best_plane = np.linalg.lstsq(design[inlier], lower_xyz[inlier, 2], rcond=None)[0]
    predicted_z = xyz[:, 0] * best_plane[0] + xyz[:, 1] * best_plane[1] + best_plane[2]
    ground_seed = stable & ~car & (np.abs(xyz[:, 2] - predicted_z) < .30)
    ground_residual = np.full(coarse_shape, np.nan)
    seed_idx = np.flatnonzero(ground_seed)
    order = seed_idx[np.argsort(cids[seed_idx], kind='stable')]
    groups, beginnings, numbers = np.unique(cids[order], return_index=True, return_counts=True)
    for cell, begin, number in zip(groups, beginnings, numbers):
        if number >= 4:
            idx = order[begin:begin + number]
            ground_residual.flat[cell] = np.median(xyz[idx, 2] - predicted_z[idx])
    missing = ~np.isfinite(ground_residual)
    distance, nearest = ndi.distance_transform_edt(missing, return_indices=True)
    filled = ground_residual[tuple(nearest)]
    correction = ndi.gaussian_filter(ndi.median_filter(filled, size=3), sigma=.8)
    yy, xx = np.indices(shape)
    floor = (origin[0] + (xx + .5) * RES) * best_plane[0] + (origin[1] + (yy + .5) * RES) * best_plane[1] + best_plane[2]
    floor += correction[yy // 5, xx // 5]
    floor_valid = distance[yy // 5, xx // 5] * COARSE <= 1.0
    heights = xyz[:, 2] - floor.flat[ids]
    on_ground = stable & ~car & (np.abs(heights) <= PARAMS['ground_tolerance_m']) & floor_valid.flat[ids]
    obstacle_points = stable & (heights >= .22) & (heights <= 2.2)
    vehicle_obstacle_points = stable & car & (heights >= .14) & (heights <= 2.2)
    if OBSTACLE_FILTER is not None:
        obstacle_points = OBSTACLE_FILTER(xyz, obstacle_points, vehicle_obstacle_points, d)
    ground_hits = counts(ids, on_ground, shape)
    obstacle_hits = counts(ids, obstacle_points, shape)
    vehicle_hits = counts(ids, vehicle_obstacle_points, shape)
    semantic_car_hits = counts(ids, stable & (np.rint(d['semantic_class']).astype(np.int32) == 1), shape)
    dynamic_hits = counts(ids, finite & dynamic, shape)
    observed_hits = counts(ids, finite, shape)
    obstacle = (obstacle_hits >= 2) | (vehicle_hits >= 1)
    # A one-cell bridge connects sparse obstacle boundaries. Robot clearance
    # remains a runtime costmap inflation setting, not baked into map geometry.
    disk1 = np.array([[0, 1, 0], [1, 1, 1], [0, 1, 0]], bool)
    obstacle = ndi.binary_dilation(obstacle, structure=disk1)
    support = ndi.convolve((ground_hits > 0).astype(np.int16), np.ones((3, 3), np.int16))
    ground_observed = (ground_hits >= 2) | ((ground_hits > 0) & (support >= 4))
    disk2 = (np.indices((5, 5)) - 2)**2
    disk2 = disk2.sum(axis=0) <= 4
    closed = ndi.binary_closing(ground_observed, structure=disk2)
    near_ground = ndi.distance_transform_edt(~ground_observed) * RES <= .2
    free_candidate = (ground_observed | (closed & near_ground)) & floor_valid
    gy, gx = np.gradient(ndi.gaussian_filter(floor, sigma=2), RES)
    slope = np.rad2deg(np.arctan(np.hypot(gx, gy)))
    cy, cx = np.indices(coarse_shape)
    coarse_floor = (origin[0] + (cx + .5) * COARSE) * best_plane[0] + (origin[1] + (cy + .5) * COARSE) * best_plane[1] + best_plane[2] + correction
    step = ndi.maximum_filter(coarse_floor, size=3) - ndi.minimum_filter(coarse_floor, size=3)
    terrain_blocked = free_candidate & ((slope > 20) | (step[yy // 5, xx // 5] > .22))
    occupied = obstacle | terrain_blocked
    free = free_candidate & ~occupied
    occupancy = np.full(shape, 205, np.uint8)
    occupancy[free] = 254
    occupancy[occupied] = 0
    geometry_occupancy = occupancy.copy()
    # Keep the purely geometric map as a separate candidate. The draft using
    # historical traversability removes blockers only where ground is supported;
    # recognized parking cars and terrain discontinuities keep priority.
    quaternions = pose_data[:, [4, 5, 6, 3]]  # file stores qw qx qy qz
    rotation = Rotation.from_quat(quaternions)
    dense_times = []
    for i in range(len(route) - 1):
        steps = max(1, int(np.ceil(np.linalg.norm(route[i + 1, :2] - route[i, :2]) / .05)))
        dense_times.extend(i + np.arange(steps) / steps)
    dense_times.append(len(route) - 1)
    dense_times = np.array(dense_times)
    dense_xy = np.c_[np.interp(dense_times, np.arange(len(route)), route[:, 0]),
                     np.interp(dense_times, np.arange(len(route)), route[:, 1])]
    matrices = Slerp(np.arange(len(route)), rotation)(dense_times).as_matrix()
    yaw = np.arctan2(matrices[:, 1, 0], matrices[:, 0, 0])
    corridor = np.zeros(shape, bool)
    for center, angle in zip(dense_xy, yaw):
        pixel = np.floor((center - origin) / RES).astype(int)
        x0, x1 = max(0, pixel[0] - 10), min(shape[1], pixel[0] + 11)
        y0, y1 = max(0, pixel[1] - 10), min(shape[0], pixel[1] + 11)
        dx = origin[0] + (xx[y0:y1, x0:x1] + .5) * RES - center[0]
        dy = origin[1] + (yy[y0:y1, x0:x1] + .5) * RES - center[1]
        longitudinal = dx * np.cos(angle) + dy * np.sin(angle)
        lateral = -dx * np.sin(angle) + dy * np.cos(angle)
        corridor[y0:y1, x0:x1] |= ((longitudinal >= -.72) & (longitudinal <= .4) & (np.abs(lateral) <= .46))
    car_protected = ndi.binary_dilation(vehicle_hits > 0, structure=disk1, iterations=2)
    trajectory_free = corridor & free_candidate & ~car_protected & ~terrain_blocked
    cleared = trajectory_free & occupied
    occupancy[trajectory_free] = 254
    route_prior_occupancy = occupancy.copy()
    # When the same route cell contains both moving returns and residual car
    # semantics, its recorded traversal is evidence the cell was driveable.
    # Do not apply this exception where repeated parked-car boxes protect it.
    route_dynamic_car = corridor & (dynamic_hits > 0) & (semantic_car_hits > 0) & ~car_protected & ~terrain_blocked
    car_semantics = np.rint(d['semantic_class']).astype(np.int32) == 1
    selected = json.loads(DETECTIONS.read_text()).get('selected_detections', [])
    tracks = {}
    for detection in selected:
        if detection.get('class') == 'car' and detection.get('track_id') is not None:
            tracks.setdefault(detection['track_id'], []).append(detection)
    moving_track_points = np.zeros(len(xyz), dtype=bool)
    moving_track_dynamic_points = np.zeros(len(xyz), dtype=bool)
    moving_track_reports = []
    for track_id, observations in tracks.items():
        observations = sorted(observations, key=lambda item: int(item['frame_index']))
        for first, second in zip(observations, observations[1:]):
            f0, f1 = int(first['frame_index']), int(second['frame_index'])
            if f0 not in frame_times or f1 not in frame_times:
                continue
            dt = frame_times[f1] - frame_times[f0]
            displacement = np.linalg.norm(np.asarray(second['center_map'][:2]) - np.asarray(first['center_map'][:2]))
            speed = displacement / dt if dt > 0 else 0.0
            if (displacement < PARAMS['minimum_car_track_displacement_m'] or
                    speed < PARAMS['minimum_car_track_speed_mps'] or
                    min(float(first['score']), float(second['score'])) < .2):
                continue
            def in_box(detection):
                center3 = np.asarray(detection['center_map'], dtype=np.float64)
                size3 = np.asarray(detection['size'], dtype=np.float64)
                yaw0 = float(detection['yaw_map'])
                delta_x, delta_y = xyz[:, 0] - center3[0], xyz[:, 1] - center3[1]
                along0 = delta_x * np.cos(yaw0) + delta_y * np.sin(yaw0)
                across0 = -delta_x * np.sin(yaw0) + delta_y * np.cos(yaw0)
                return ((np.abs(along0) <= size3[0] / 2) &
                        (np.abs(across0) <= size3[1] / 2) &
                        (np.abs(xyz[:, 2] - center3[2]) <= size3[2] / 2))
            box0, box1 = in_box(first), in_box(second)
            combined_box = box0 | box1
            dynamic_inside = dynamic & combined_box
            if not np.any(dynamic_inside):
                continue
            moving_track_dynamic_points |= dynamic_inside
            car_residual = stable & car_semantics & ~car & combined_box
            moving_track_points |= car_residual
            moving_track_reports.append(dict(track_id=str(track_id), frames=[f0, f1],
                                             center_displacement_m=round(float(displacement), 3),
                                             elapsed_seconds=round(float(dt), 3),
                                             speed_mps=round(float(speed), 3),
                                             dynamic_points_in_boxes=int(dynamic_inside.sum()),
                                             stable_car_points_in_boxes=int(car_residual.sum())))
    track_car_hits = counts(ids, moving_track_points, shape)
    track_dynamic_hits = counts(ids, moving_track_dynamic_points, shape)
    if APPLY_LOCAL_CAR_FILTER:
        route_track_car = corridor & ((track_car_hits > 0) | (track_dynamic_hits > 0)) & ~car_protected & ~terrain_blocked
        route_dynamic_vehicle = route_dynamic_car | route_track_car
        dynamic_car_trajectory_cleared = route_dynamic_vehicle & (occupancy == 0)
        direct_dynamic_car_cleared = dynamic_car_trajectory_cleared.copy()
        occupancy[dynamic_car_trajectory_cleared] = 254
        # Clear only evidence-free obstacle-connection cells attached to a
        # confirmed moving-car cell in the historical footprint.
        halo = ndi.binary_dilation(route_dynamic_vehicle, structure=disk1)
        halo_cleared = halo & corridor & (occupancy == 0) & (observed_hits == 0) & ~terrain_blocked & ~car_protected
        occupancy[halo_cleared] = 254
        dynamic_car_trajectory_cleared |= halo_cleared
    else:
        route_dynamic_vehicle = np.zeros(shape, bool)
        dynamic_car_trajectory_cleared = np.zeros(shape, bool)
        direct_dynamic_car_cleared = np.zeros(shape, bool)
        halo_cleared = np.zeros(shape, bool)
    free = occupancy == 254
    occupied = occupancy == 0
    confidence_min = np.full(np.prod(shape), np.inf, np.float32)
    np.minimum.at(confidence_min, ids[finite], d['confidence'][finite])
    confidence_min[~np.isfinite(confidence_min)] = np.nan
    confidence_min = confidence_min.reshape(shape)
    confidence_sum = np.bincount(ids[finite], weights=d['confidence'][finite], minlength=np.prod(shape)).reshape(shape)
    confidence_mean = np.divide(confidence_sum, observed_hits, out=np.full(shape, np.nan), where=observed_hits > 0)

    pgm('map.pgm', occupancy)
    pgm('map_geometry.pgm', geometry_occupancy)
    pgm('map_route_prior.pgm', route_prior_occupancy)
    pgm('obstacle.pgm', occupancy)
    pgm('obstacle_evidence.pgm', np.where(obstacle, 0, np.where(free_candidate, 254, 205)))
    pgm('confidence.pgm', np.where(np.isfinite(confidence_mean), np.nan_to_num(confidence_mean) * 254, 255))
    elevation_valid = floor_valid & (free_candidate | occupied)
    elevation_min = float(floor[elevation_valid].min())
    elevation_max = float(floor[elevation_valid].max())
    pgm('elevation.pgm', np.where(elevation_valid, np.clip((floor - elevation_min) / (elevation_max - elevation_min) * 254, 0, 254), 255))
    pgm('slope.pgm', np.where(elevation_valid, np.clip(slope / 45 * 254, 0, 254), 255))
    map_config = dict(image='map.pgm', mode='trinary', resolution=RES,
                      origin=[float(origin[0]), float(origin[1]), 0.0],
                      negate=0, occupied_thresh=.65, free_thresh=.196)
    (OUT / 'map.yaml').write_text(yaml.safe_dump(map_config, sort_keys=False))
    (OUT / 'map_geometry.yaml').write_text(yaml.safe_dump({**map_config, 'image': 'map_geometry.pgm'}, sort_keys=False))
    (OUT / 'map_route_prior.yaml').write_text(yaml.safe_dump({**map_config, 'image': 'map_route_prior.pgm'}, sort_keys=False))
    layer_meta = dict(resolution=RES, origin=origin.tolist(), shape=list(shape),
                      confidence=dict(image='confidence.pgm', scale='byte/254', unknown_byte=255, aggregation='observed-cell mean; not occupancy'),
                      elevation=dict(image='elevation.pgm', minimum_m=elevation_min, maximum_m=elevation_max, unknown_byte=255),
                      slope=dict(image='slope.pgm', minimum_degrees=0, maximum_degrees=45, unknown_byte=255),
                      obstacle=dict(image='obstacle.pgm', encoding='same trinary encoding as map.pgm'),
                      source_pose_semantics='T_map_mapping_body', navigation_frame='source optimized map frame')
    (OUT / 'layer_metadata.yaml').write_text(yaml.safe_dump(layer_meta, sort_keys=False))
    np.savez_compressed(OUT / 'layers.npz', occupancy=occupancy, ground_height=floor.astype(np.float32),
                        ground_model_valid=floor_valid, slope_degrees=slope.astype(np.float32),
                        confidence_min=confidence_min, confidence_mean=confidence_mean.astype(np.float32),
                        ground_hits=ground_hits.astype(np.uint32), obstacle_hits=obstacle_hits.astype(np.uint32),
                        vehicle_hits=vehicle_hits.astype(np.uint32), dynamic_hits=dynamic_hits.astype(np.uint32),
                        geometry_occupancy=geometry_occupancy, trajectory_corridor=corridor, trajectory_cleared=cleared,
                        semantic_car_hits=semantic_car_hits.astype(np.uint32), track_car_hits=track_car_hits.astype(np.uint32),
                        dynamic_car_trajectory_cleared=dynamic_car_trajectory_cleared,
                        origin=origin, resolution=RES)

    extent = [origin[0], origin[0] + shape[1] * RES, origin[1], origin[1] + shape[0] * RES]
    cmap = LinearSegmentedColormap.from_list('confidence', ['#2e3ddb', '#00bfdc', '#26bf4d', '#f2cc1f', '#db1a1a'])
    cmap.set_bad('#dddddd')
    fig, axes = plt.subplots(1, 3, figsize=(19, 7), constrained_layout=True)
    display = np.ones(shape, np.uint8)
    display[free] = 2
    display[occupied] = 0
    axes[0].imshow(display, origin='lower', extent=extent, cmap=ListedColormap(['black', '#cdcdcd', 'white']), vmin=0, vmax=2, interpolation='nearest')
    axes[0].plot(route[:, 0], route[:, 1], color='#168af4', lw=1, label='Optimized mapping route')
    route_pixels = np.floor((route[:, :2] - origin) / RES).astype(int)
    review = occupancy[route_pixels[:, 1], route_pixels[:, 0]] == 0
    if review.any():
        axes[0].scatter(route[review, 0], route[review, 1], facecolors='none', edgecolors='#d40000', s=75, linewidths=1.5, label='Obstacle conflict: review')
    cleared_y, cleared_x = np.nonzero(dynamic_car_trajectory_cleared)
    if len(cleared_x):
        axes[0].scatter(origin[0] + (cleared_x + .5) * RES, origin[1] + (cleared_y + .5) * RES,
                        color='#00b9c7', s=20, label='Filtered dynamic car residual')
    axes[0].legend(loc='lower right', fontsize=8)
    axes[0].set_title('Navigation draft + historical route prior\nblack: occupied; white: ground free; gray: unknown')
    im = axes[1].imshow(confidence_mean, origin='lower', extent=extent, cmap=cmap, vmin=0, vmax=1, interpolation='nearest')
    axes[1].set_title('Static confidence: observed-cell mean\nThis layer is separate from occupancy')
    fig.colorbar(im, ax=axes[1], shrink=.7, label='Static confidence (heuristic)')
    diagnostic = np.full((*shape, 3), .88, np.float32)
    diagnostic[free] = [1, 1, 1]
    diagnostic[obstacle] = [.1, .1, .1]
    diagnostic[dynamic_hits > 0] = [1, .55, .1]
    diagnostic[vehicle_hits > 0] = [.15, .75, .3]
    axes[2].imshow(diagnostic, origin='lower', extent=extent, interpolation='nearest')
    axes[2].set_title('Evidence audit\ngreen: parked vehicle; orange: removed dynamic returns')
    for ax in axes:
        ax.set_xlabel('Map X (m)')
        ax.set_ylabel('Map Y (m)')
        ax.set_aspect('equal')
    fig.suptitle('Scheme 1: 2D confidence and navigation map — 0.10 m/cell', fontsize=17)
    fig.savefig(OUT / 'navigation_preview.png', dpi=170)
    plt.close(fig)
    Image.fromarray(np.flipud(occupancy)).save(OUT / 'occupancy.png')
    if np.any(dynamic_car_trajectory_cleared):
        focus_cells = (route_dynamic_car & (dynamic_hits > 0) & (semantic_car_hits > 0) & direct_dynamic_car_cleared)
        if not np.any(focus_cells):
            focus_cells = direct_dynamic_car_cleared if np.any(direct_dynamic_car_cleared) else dynamic_car_trajectory_cleared
        fy, fx = np.argwhere(focus_cells)[0]
        focus = origin + (np.array([fx, fy]) + .5) * RES
        fig, axes = plt.subplots(1, 2, figsize=(13, 6), constrained_layout=True)
        for panel, (ax, layer, title) in enumerate([
            (axes[0], route_prior_occupancy, 'Before targeted filter: residual vehicle points block the route'),
            (axes[1], occupancy, 'After filter: track-supported car remnant released; other obstacles remain'),
        ]):
            ax.imshow(layer, origin='lower', extent=extent, cmap=ListedColormap(['black', '#cdcdcd', 'white']),
                      vmin=0, vmax=254, interpolation='nearest')
            ax.plot(route[:, 0], route[:, 1], color='#168af4', lw=1.2, label='Mapping route')
            ax.add_patch(Rectangle(focus - RES / 2, RES, RES, fill=False,
                                    edgecolor='#e60000' if panel == 0 else '#00a8a8',
                                    linewidth=2.5))
            ax.set_xlim(focus[0] - 2, focus[0] + 2)
            ax.set_ylim(focus[1] - 2, focus[1] + 2)
            ax.set_aspect('equal')
            ax.set_title(title)
            ax.set_xlabel('Map X (m)')
            ax.set_ylabel('Map Y (m)')
        axes[1].legend(loc='lower right')
        fig.suptitle(f'Targeted cell at X={focus[0]:.2f} m, Y={focus[1]:.2f} m', fontsize=14)
        fig.savefig(OUT / 'dynamic_filter_comparison.png', dpi=170)
        plt.close(fig)

    # Check serialization, coordinate convention, and priority rules directly.
    reopened = np.asarray(Image.open(OUT / 'map.pgm'))
    assert np.array_equal(reopened, np.flipud(occupancy))
    assert set(np.unique(reopened)).issubset({0, 205, 254})
    assert yaml.safe_load((OUT / 'map.yaml').read_text()) == map_config
    assert np.all(occupancy[vehicle_hits > 0] == 0)
    assert np.all(occupancy[terrain_blocked] == 0)
    assert not np.any(cleared & car_protected)
    assert not np.any(dynamic_car_trajectory_cleared & (vehicle_hits > 0))
    assert not np.any(dynamic_car_trajectory_cleared & terrain_blocked)
    assert np.all(occupancy[dynamic_car_trajectory_cleared] == 254)
    unchanged = ~(trajectory_free | dynamic_car_trajectory_cleared)
    assert np.array_equal(occupancy[unchanged], geometry_occupancy[unchanged])
    assert not np.any(on_ground & dynamic) and not np.any(obstacle_points & dynamic)
    route_ij = np.floor((route[:, :2] - origin) / RES).astype(int)
    inside = (route_ij[:, 0] >= 0) & (route_ij[:, 0] < shape[1]) & (route_ij[:, 1] >= 0) & (route_ij[:, 1] < shape[0])
    route_vals = occupancy[route_ij[inside, 1], route_ij[inside, 0]]
    conflict_indices = np.flatnonzero(inside)[route_vals == 0]
    route_prior_vals = route_prior_occupancy[route_ij[inside, 1], route_ij[inside, 0]]
    route_prior_conflict_indices = np.flatnonzero(inside)[route_prior_vals == 0]
    report = dict(status='offline_preview_not_runtime_validated', source_pcd=str(SOURCE), source_sha256=checksum,
                  source_trajectory=str(POSES), parameters=PARAMS, point_count=len(xyz),
                  dynamic_points_excluded=int(np.count_nonzero(finite & dynamic)),
                  parked_vehicle_points_retained=int(np.count_nonzero(vehicle_obstacle_points)),
                  ground_points=int(np.count_nonzero(on_ground)), ground_plane_abc=best_plane.tolist(),
                  coarse_ground_consensus_cells=int(best_score), grid_shape=list(shape), origin=origin.tolist(),
                  free_cells=int(np.count_nonzero(free)), occupied_cells=int(np.count_nonzero(occupied)),
                  unknown_cells=int(np.count_nonzero(occupancy == 205)),
                  locally_interpolated_free_cells=int(np.count_nonzero(free & ~ground_observed)),
                  terrain_blocked_cells=int(np.count_nonzero(terrain_blocked)),
                  route_samples=len(route), route_cell_counts={str(int(k)): int(np.count_nonzero(route_vals == k)) for k in [0, 205, 254]},
                  local_car_filter_applied=APPLY_LOCAL_CAR_FILTER,
                  trajectory_cleared_occupied_cells=int(np.count_nonzero(cleared)),
                  dynamic_car_filter_candidate_cells=int(np.count_nonzero(route_dynamic_vehicle)),
                  dynamic_car_direct_cells_cleared=int(np.count_nonzero(direct_dynamic_car_cleared)),
                  dynamic_car_no_return_halo_cells_cleared=int(np.count_nonzero(halo_cleared)),
                  dynamic_car_filter_cleared_cells=int(np.count_nonzero(dynamic_car_trajectory_cleared)),
                  moving_car_tracks_supporting_filter=moving_track_reports if APPLY_LOCAL_CAR_FILTER else [],
                  dynamic_car_filter_audit=[
                      dict(grid_xy=[int(cx), int(cy)], center_map_xy=(origin + (np.array([cx, cy]) + .5) * RES).tolist(),
                           all_source_points_in_cell=int(np.count_nonzero(ids == cy * shape[1] + cx)),
                           dynamic_points_in_cell=int(dynamic_hits[cy, cx]),
                           stable_car_semantic_points_in_cell=int(semantic_car_hits[cy, cx]),
                           track_supported_dynamic_points_in_cell=int(track_dynamic_hits[cy, cx]),
                           accepted_parked_vehicle_points_in_cell=int(vehicle_hits[cy, cx]),
                           occupancy_before=int(route_prior_occupancy[cy, cx]), occupancy_after=int(occupancy[cy, cx]))
                      for cy, cx in np.argwhere(dynamic_car_trajectory_cleared)],
                  route_prior_route_cell_counts={str(int(k)): int(np.count_nonzero(route_prior_occupancy[route_ij[inside, 1], route_ij[inside, 0]] == k)) for k in [0, 205, 254]},
                  geometry_route_cell_counts={str(int(k)): int(np.count_nonzero(geometry_occupancy[route_ij[inside, 1], route_ij[inside, 0]] == k)) for k in [0, 205, 254]},
                  remaining_route_obstacle_samples=[dict(pose_index=int(i), xyz=route[i].tolist()) for i in conflict_indices],
                  route_prior_obstacle_samples=[dict(pose_index=int(i), xyz=route[i].tolist()) for i in route_prior_conflict_indices],
                  elapsed_seconds=round(time.monotonic() - start, 3),
                  limitations=['Dominant-ground-plane plus local residual model; multi-level areas require review.',
                               'No ray casting: areas lacking ground support stay unknown.',
                               'Minimum confidence raster preserves overlapping weak evidence; mean raster is visualization only.',
                               'Heuristic confidence is not an occupancy probability; parking cars remain occupied.',
                               'Historical route prior assumes nominal converter footprint (front .4/rear .72/half width .46 m); lidar-to-base calibration is unavailable and requires review.',
                               'Route prior clears ground-supported cells and preserves accepted vehicle cells and terrain blocks.',
                               'Dynamic car filter uses mixed car/dynamic cells and matched moving-car track boxes in the historical route corridor; accepted parked-car masks and terrain blocks take priority.',
                               'map_geometry.yaml retains the geometry-only candidate; map_route_prior.yaml retains the route-only candidate before semantic dynamic filtering.',
                               'No Nav2 planner or physical navigation validation performed.'],
                  checks=['PGM round trip and vertical flip', 'YAML fields', 'parked vehicle occupancy priority',
                          'dynamic exclusion', 'input SHA256 validation disabled', 'route clearing preserves vehicle and terrain obstacles',
                          'localized semantic car/dynamic filter leaves accepted parked-car cells occupied'])
    (OUT / 'conversion_report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
