#!/usr/bin/env python3
"""Offline CenterPoint postprocessing and point-supported confidence refinement.

Local heuristic association, not the official CenterPoint tracker. Original
geometry, visibility motion and support fields are preserved. No box raster
or interpolated trajectory is used as a removal mask.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.optimize import linear_sum_assignment
from scipy.spatial import cKDTree
from scipy.spatial.transform import Rotation

HERE = Path(__file__).resolve().parent
TRIAL = HERE.parent.parent



def read_pcd(path):
    with Path(path).open("rb") as stream:
        header = []
        while True:
            line = stream.readline()
            if not line:
                raise ValueError(f"Missing PCD DATA: {path}")
            header.append(line)
            if line.startswith(b"DATA "):
                if line.strip() != b"DATA binary":
                    raise ValueError(f"Unsupported PCD encoding: {path}")
                break
        meta = {}
        for line in header:
            parts = line.decode("ascii").strip().split(maxsplit=1)
            if len(parts) == 2:
                meta[parts[0]] = parts[1]
        fields = meta["FIELDS"].split()
        if (meta["SIZE"].split() != ["4"] * len(fields)
                or meta["TYPE"].split() != ["F"] * len(fields)
                or meta["COUNT"].split() != ["1"] * len(fields)):
            raise ValueError("Expected scalar float32 PCD fields")
        data = np.frombuffer(stream.read(), dtype="<f4").copy()
        if data.size != int(meta["POINTS"]) * len(fields):
            raise ValueError("PCD payload does not match declared point count")
        return header, fields, data.reshape(-1, len(fields))


def write_pcd(path, header, fields, data, extra=()):
    header = list(header)
    updates = {"FIELDS": fields + list(extra), "SIZE": ["4"] * data.shape[1],
               "TYPE": ["F"] * data.shape[1], "COUNT": ["1"] * data.shape[1]}
    for i, line in enumerate(header):
        key = line.decode("ascii").split()[0]
        if key in updates:
            header[i] = (key + " " + " ".join(updates[key]) + "\n").encode("ascii")
    with Path(path).open("wb") as stream:
        stream.writelines(header)
        stream.write(np.asarray(data, dtype="<f4").tobytes())


def inside_body_box(points, box, trim=0, z_reference="bottom"):
    delta = points - np.asarray(box[:3])
    yaw = box[6]
    x = delta[:, 0] * np.cos(yaw) + delta[:, 1] * np.sin(yaw)
    y = -delta[:, 0] * np.sin(yaw) + delta[:, 1] * np.cos(yaw)
    if z_reference not in ("bottom", "center"):
        raise ValueError("box_z_reference must be bottom or center")
    bottom = 0 if z_reference == "bottom" else -box[5] / 2
    return ((np.abs(x) <= box[3] / 2) & (np.abs(y) <= box[4] / 2)
            & (delta[:, 2] >= bottom + trim) & (delta[:, 2] <= bottom + box[5]))


def filter_detections(raw, config):
    frames = defaultdict(list)
    rejected = Counter()
    for source_id, row in enumerate(raw):
        cls = row["class"]
        if cls not in ("car", "pedestrian"):
            continue
        if row["score"] < config["minimum_detection_score"][cls]:
            rejected[f"{cls}_score"] += 1
        elif row["current_sweep_points_in_box"] < config["minimum_current_sweep_points"][cls]:
            rejected[f"{cls}_support"] += 1
        else:
            frames[int(row["frame_index"])].append(dict(row, source_detection_id=source_id))
    selected = []
    for frame in sorted(frames):
        kept = []
        for row in sorted(frames[frame], key=lambda d: (-d["score"], d["source_detection_id"])):
            duplicate = any(row["class"] == other["class"] and np.linalg.norm(
                np.asarray(row["center_map"][:2]) - other["center_map"][:2]
            ) < config["nms_radius_m"][row["class"]] for other in kept)
            if duplicate:
                rejected[f'{row["class"]}_nms'] += 1
            else:
                kept.append(row)
        selected.extend(kept)
    return selected, dict(rejected)


def motion_metrics(rows):
    t = np.array([d["stamp"] for d in rows], dtype=float)
    t -= t[0]
    xy = np.array([d["center_map"][:2] for d in rows], dtype=float)
    if len(rows) < 2 or t[-1] <= 0:
        return {"speed_mps": 0., "displacement_m": 0., "fit_r2": 0., "span_s": 0.}
    design = np.c_[np.ones(len(t)), t]
    coef = np.linalg.lstsq(design, xy, rcond=None)[0]
    residual = np.sum((xy - design @ coef) ** 2)
    variance = np.sum((xy - xy.mean(axis=0)) ** 2)
    # Endpoint medians limit the effect of a single jumping box center.
    n = max(1, min(3, len(rows) // 3))
    displacement = np.linalg.norm(np.median(xy[-n:], axis=0) - np.median(xy[:n], axis=0))
    return {"speed_mps": float(np.linalg.norm(coef[1])), "displacement_m": float(displacement),
            "fit_r2": float(max(0., 1 - residual / max(variance, 1e-12))), "span_s": float(t[-1])}


def confirms_motion(rows, config):
    m = motion_metrics(rows)
    return (len(rows) >= config["minimum_track_frames"]
            and m["span_s"] >= config["minimum_track_span_s"]
            and m["speed_mps"] >= config["minimum_motion_speed_mps"]
            and m["displacement_m"] >= config["minimum_motion_displacement_m"]
            and m["fit_r2"] >= config["minimum_motion_fit_r2"])


def associate(selected, config):
    """One-to-one map-frame assignment with size, gap and speed gates."""
    tracks = []
    frames = defaultdict(list)
    for d in selected:
        frames[d["frame_index"]].append(d)
    for frame in sorted(frames):
        for cls in ("pedestrian", "car"):
            rows = [d for d in frames[frame] if d["class"] == cls]
            if not rows:
                continue
            stamp = rows[0]["stamp"]
            active = [tr for tr in tracks if tr["class"] == cls
                      and 0 < stamp - tr["detections"][-1]["stamp"] <= config["maximum_track_gap_s"]]
            cost = np.full((len(active), len(rows)), 1e6)
            for i, tr in enumerate(active):
                last = tr["detections"][-1]
                dt = stamp - last["stamp"]
                previous = np.asarray(last["center_map"][:2])
                velocity = np.zeros(2)
                if len(tr["detections"]) >= 2:
                    earlier = tr["detections"][-2]
                    velocity = (previous - earlier["center_map"][:2]) / (last["stamp"] - earlier["stamp"])
                    speed = np.linalg.norm(velocity)
                    velocity *= min(1., config["maximum_association_speed_mps"][cls] / max(speed, 1e-9))
                prediction = previous + velocity * dt
                gate = config["association_position_slack_m"][cls] + config["maximum_association_speed_mps"][cls] * dt
                for j, d in enumerate(rows):
                    size_ratio = np.maximum(np.asarray(d["size"]) / last["size"],
                                            np.asarray(last["size"]) / d["size"])
                    if np.max(size_ratio) > config["maximum_box_size_ratio"]:
                        continue
                    distance = np.linalg.norm(np.asarray(d["center_map"][:2]) - previous)
                    dz = abs(d["center_map"][2] - last["center_map"][2])
                    if distance > gate or dz > max(0.8, last["size"][2]):
                        continue
                    error = np.linalg.norm(np.asarray(d["center_map"][:2]) - prediction)
                    cost[i, j] = error + 0.25 * distance + 0.2 * np.sum(np.abs(np.log(size_ratio)))
            used = set()
            if cost.size:
                ii, jj = linear_sum_assignment(cost)
                for i, j in zip(ii, jj):
                    if cost[i, j] >= 1e6:
                        continue
                    active[i]["detections"].append(rows[j])
                    used.add(int(j))
            for j, d in enumerate(rows):
                if j not in used:
                    tracks.append({"track_id": f"{cls}_{len(tracks):04d}", "class": cls, "detections": [d]})
    for tr in tracks:
        rows = tr["detections"]
        metrics = motion_metrics(rows)
        repeated = len(rows) >= config["minimum_track_frames"] and metrics["span_s"] >= config["minimum_track_span_s"]
        moving = confirms_motion(rows, config)
        # Local windows also capture turns/stop-start motion that a global line
        # fit cannot represent. Only observations in those windows get the flag.
        local_moving_ids = set()
        for begin in range(len(rows)):
            window = rows[begin:begin + 5]
            if confirms_motion(window, config):
                local_moving_ids.update(d["source_detection_id"] for d in window)
        if moving:
            local_moving_ids.update(d["source_detection_id"] for d in rows)
        state = "moving" if local_moving_ids else ("stationary" if repeated and metrics["displacement_m"] < 0.5
                                                  and metrics["speed_mps"] < 0.15 else "uncertain")
        tr.update(metrics, state=state, distinct_frames=len(rows), repeated=repeated)
        for d in rows:
            d.update(track_id=tr["track_id"], movement_state="moving" if d["source_detection_id"] in local_moving_ids
                     else state if state != "moving" else "uncertain", track_repeated=repeated)
    return tracks


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=HERE)
    parser.add_argument("--config", type=Path, default=HERE / "centerpoint_config.json")
    parser.add_argument("--detections", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--parked", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--package", type=Path, required=True)
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)
    raw_path = args.detections
    source = args.source
    baseline_path = args.baseline
    parked_path = args.parked
    evidence_path = args.evidence
    raw = json.loads(raw_path.read_text())
    package = args.package
    poses_path = package / "poses_timed.txt"
    inputs = [raw_path, source, baseline_path, parked_path, evidence_path, poses_path]
    header, fields, data = read_pcd(source)
    col = {name: i for i, name in enumerate(fields)}
    xyz = data[:, [col[k] for k in ("x", "y", "z")]].astype(float)
    tree = cKDTree(xyz)
    selected, rejection = filter_detections(raw["detections"], config)
    poses = {}
    for line in poses_path.read_text().splitlines():
        r = line.split()
        p = np.array(r[2:9], float)
        poses[int(r[0].split(".")[0])] = (float(r[1]), p[:3], Rotation.from_quat(p[[4, 5, 6, 3]]))
    # Recompute map centers using the verified optimized poses, rather than
    # silently trusting potentially differently transformed detector metadata.
    center_errors = []
    for d in selected:
        stamp, translation, rotation = poses[d["frame_index"]]
        if abs(stamp - d["stamp"]) > 1e-3:
            raise ValueError("Detector/keyframe timestamps disagree")
        center = rotation.apply(d["box_body"][:3]) + translation
        center_errors.append(float(np.linalg.norm(center - d["center_map"])))
        d["center_map"] = center.tolist()
    tracks = associate(selected, config)
    frames = defaultdict(list)
    for d in selected:
        frames[d["frame_index"]].append(d)
    parked = json.loads(parked_path.read_text())["clusters"]
    parked_masks = []
    for c in parked:
        delta = xyz - c["center_map_xyz"]
        yaw = c["yaw_map_rad"]
        along = delta[:, 0] * np.cos(yaw) + delta[:, 1] * np.sin(yaw)
        across = -delta[:, 0] * np.sin(yaw) + delta[:, 1] * np.cos(yaw)
        size = c["box_size_xyz"]
        # Spatial references establish location only. Frame-local oriented boxes
        # and actual returns establish the 3D support, including ego roll/pitch.
        parked_masks.append((np.abs(along) <= size[0]/2 + .12) & (np.abs(across) <= size[1]/2 + .12))
    candidate_sets = defaultdict(set)
    outside_support = np.zeros(len(data), dtype=np.uint16)
    scan_stats = []
    for frame in sorted(poses):
        patch_path = package / "patches" / f"{frame}.pcd"
        _, names, patch = read_pcd(patch_path)
        body = patch[:, [names.index(k) for k in ("x", "y", "z")]].astype(float)
        _, translation, rotation = poses[frame]
        world = rotation.apply(body) + translation
        distance, map_ids = tree.query(world)
        match = distance <= config["map_point_match_radius_m"]
        object_union = np.zeros(len(body), bool)
        for d in frames[frame]:
            # Untrimmed box excludes potential objects from background votes.
            object_union |= inside_body_box(body, d["box_body"], z_reference=config["box_z_reference"])
            mask = inside_body_box(body, d["box_body"], config["box_bottom_trim_m"], config["box_z_reference"])
            mask &= np.linalg.norm(body[:, :2], axis=1) >= config["self_point_radius_m"]
            ids = np.unique(map_ids[mask & match])
            d["supported_map_points"] = int(len(ids))
            d["support_projection_points"] = int(mask.sum())
            candidate_sets[d["source_detection_id"]].update(ids.tolist())
        outside = (~object_union) & (distance <= config["background_match_radius_m"])
        outside_support[np.unique(map_ids[outside])] += 1
        scan_stats.append({"frame": frame, "scan_points": len(body), "matched_points": int(match.sum())})
    evidence = np.load(evidence_path)
    indices = evidence["indices"].astype(np.int64)
    if np.any(indices < 0) or np.any(indices >= len(data)):
        raise ValueError("Invalid motion-evidence point indices")
    override = np.zeros(len(data), bool)
    override[indices[evidence["dynamic_mask"].astype(bool)]] = True
    motion = np.clip(data[:, col["motion_score"]], 0, 1)
    base = 1 - np.maximum(motion, override)
    background = (outside_support >= config["background_minimum_distinct_frames"]) & (motion == 0) & ~override
    person = np.zeros(len(data), bool)
    moving = np.zeros(len(data), bool)
    parked_points = np.zeros(len(data), bool)
    person_track = np.full(len(data), -1, dtype=np.float32)
    protected = set()
    parked_seeds = [set() for _ in parked]
    for d in selected:
        ids = np.array(sorted(candidate_sets[d["source_detection_id"]]), dtype=int)
        allowed = ids[~background[ids]]
        if d["class"] == "pedestrian" and d["track_repeated"]:
            person[allowed] = True
            person_track[allowed] = int(d["track_id"].rsplit("_", 1)[1])
            protected.update(ids[background[ids]].tolist())
        # New low-confidence motion refinement is scoped to people. Car-center
        # jumps in this pretrained detector must not turn reviewed parked cars
        # into dynamic objects. Existing visibility dynamics still apply to all.
        if d["class"] == "pedestrian" and d["movement_state"] == "moving":
            moving[allowed] = True
            protected.update(ids[background[ids]].tolist())
        if d["class"] == "car":
            for i, c in enumerate(parked):
                if np.linalg.norm(np.asarray(d["center_map"][:2]) - c["center_map_xyz"][:2]) <= config["parked_location_match_radius_m"]:
                    # Retain repeated spatial car evidence, but only project
                    # currently observed points, never the whole map-frame box.
                    supported = allowed[parked_masks[i][allowed]]
                    parked_seeds[i].update(supported.tolist())
    source_car = (np.rint(data[:, col["semantic_class"]]) == 1) & (data[:, col["semantic_confidence"]] > 0)
    parked_location_reports = []
    for i, c in enumerate(parked):
        seeds = np.array(sorted(parked_seeds[i]), dtype=int)
        parked_points[seeds] = True
        recovered = np.array([], dtype=int)
        if len(seeds):
            # Nearby legacy car-labelled surface points add dense map coverage,
            # but geometry alone can never spread scores across a whole box.
            candidates = np.flatnonzero(parked_masks[i] & source_car & ~background)
            distances = cKDTree(xyz[seeds]).query(xyz[candidates])[0]
            recovered = candidates[distances <= config["parked_surface_support_radius_m"]]
            parked_points[recovered] = True
        parked_location_reports.append({"center_map_xyz": c["center_map_xyz"],
            "observed_seed_points": len(seeds), "supported_car_surface_points": len(recovered),
            "distinct_reference_frames": c["distinct_frames"]})
    confidence = base.copy()
    confidence[parked_points] = np.minimum(confidence[parked_points], config["parked_car_confidence"])
    confidence[person] = np.minimum(confidence[person], config["person_confidence_without_confirmed_motion"])
    confidence[moving] = np.minimum(confidence[moving], config["moving_object_confidence"])
    added = ["base_static_confidence", "vehicle_box_mask", "dynamic_override", "parked_car_mask",
             "pedestrian_mask", "centerpoint_motion_mask", "background_protected", "pedestrian_track_id"]
    output = np.column_stack([data, base, parked_points, override, parked_points, person, moving,
                              background, person_track]).astype(np.float32)
    output[:, col["confidence"]] = confidence
    # Reconcile legacy summary fields with the new confidence formula. The old
    # semantic fields remain detector provenance, not the active scoring rule.
    output[:, col["transience_score"]] = 1 - confidence
    output_path = out / "map_centerpoint_people_cars_confidence.pcd"
    write_pcd(output_path, header, fields, output, added)
    scalar_header = [b"# Confidence display\n", b"VERSION .7\n", b"FIELDS x y z intensity\n",
                     b"SIZE 4 4 4 4\n", b"TYPE F F F F\n", b"COUNT 1 1 1 1\n",
                     f"WIDTH {len(data)}\n".encode(), b"HEIGHT 1\n", b"VIEWPOINT 0 0 0 1 0 0 0\n",
                     f"POINTS {len(data)}\n".encode(), b"DATA binary\n"]
    write_pcd(out / "map_static_confidence.pcd", scalar_header, ["x", "y", "z", "intensity"], np.c_[xyz, confidence])
    _, baseline_fields, baseline = read_pcd(baseline_path)
    before = baseline[:, baseline_fields.index("confidence")]
    percentiles = lambda values: np.percentile(values, [0, 10, 50, 90, 100]).tolist() if len(values) else []
    report = {
        "status": "offline_review", "config": config, "point_count": len(data), "output_pcd": str(output_path),
        "formula": "base=1-max(original_motion_score,visibility_dynamic_mask); confidence=min(base,parked_car_confidence for supported parked-car points,person_confidence_without_confirmed_motion for repeated person points,moving_object_confidence for motion-confirmed person points)",
        "raw_detection_counts": dict(Counter(d["class"] for d in raw["detections"])),
        "selected_detection_counts": dict(Counter(d["class"] for d in selected)), "rejection_counts": rejection,
        "selected_person_detections_inside_old_3m_exclusion": int(sum(d["class"] == "pedestrian" and np.linalg.norm(d["box_body"][:2]) < 3 for d in selected)),
        "optimized_center_max_metadata_error_m": max(center_errors, default=0),
        "box_z_reference": config["box_z_reference"],
        "box_z_reference_evidence": "MMDetection3D LiDARInstance3DBoxes default bottom center; near-person raw scan support fits bottom interpretation better. Original exporter not located; reference remains configurable.",
        "track_counts": dict(Counter(f'{t["class"]}:{t["state"]}' for t in tracks)),
        "parked_reference_locations": len(parked), "parked_car_points": int(parked_points.sum()),
        "parked_locations_with_supported_points": sum(bool(c["observed_seed_points"]) for c in parked_location_reports),
        "parked_locations": parked_location_reports,
        "person_points": int(person.sum()), "motion_confirmed_points": int(moving.sum()),
        "motion_confirmed_person_points": int((moving & person).sum()),
        "protected_background_candidate_points": len(protected), "visibility_dynamic_points": int(override.sum()),
        "parked_nonmoving_confidence_percentiles": percentiles(confidence[parked_points & ~moving & ~person]),
        "moving_person_confidence_percentiles": percentiles(confidence[moving & person]),
        "person_other_confidence_percentiles": percentiles(confidence[person & ~moving]),
        "confidence_increased_vs_old": int((confidence > before + 1e-6).sum()),
        "confidence_decreased_vs_old": int((confidence < before - 1e-6).sum()),
        "scans": scan_stats, "limitations": [
            "Heuristic scores are not calibrated probabilities or measured disappearance times.",
            "Association is a local implementation, not official CenterPoint tracking; IDs may split/swap under occlusion or crossing.",
            "No synthetic boxes across missed frames; a missed detection may leave residual points.",
            "Pretrained nuScenes model is not fine-tuned for MID360; box-contained background can still receive a score.",
            "Three repeated outside-box observations conservatively protect background; repeated motion returns can also be protected.",
            "Parked locations reuse the 19 previously reviewed spatial clusters; new unseen parked cars require additional review.",
            "Legacy semantic_score/semantic_class/semantic_confidence fields are provenance only; confidence is the active output."]}
    audit_path = HERE.parent / "scheme_1_staged/navigation_preview/diagnosis/bcd_temporal_review/bcd_temporal_report.json"
    if audit_path.exists():
        audit = json.loads(audit_path.read_text())
        cells = {}
        for label, cell in audit["cells"].items():
            distances, ids = tree.query(cell["map_xyz"])
            if np.max(distances) > .01:
                raise ValueError("Diagnostic B/C/D points no longer match map")
            cells[label] = {"points": len(ids), "before_confidence": before[ids].tolist(),
                            "after_confidence": confidence[ids].tolist(), "person_mask": person[ids].tolist(),
                            "motion_confirmed": moving[ids].tolist(), "background_protected": background[ids].tolist()}
        report["bcd_regression"] = cells
    # Read the emitted artifact independently and verify payload preservation.
    _, emitted_fields, emitted = read_pcd(output_path)
    if not np.array_equal(emitted[:, [emitted_fields.index(k) for k in ("x", "y", "z", "motion_score", "support_confidence")]],
                          data[:, [col[k] for k in ("x", "y", "z", "motion_score", "support_confidence")]]):
        raise AssertionError("Original geometry or motion/support evidence changed")
    assert np.isfinite(confidence).all() and np.all((confidence >= 0) & (confidence <= 1))
    assert np.all(confidence[override] == 0)
    assert np.all(confidence[moving] <= config["moving_object_confidence"] + 1e-6)
    assert np.all(confidence <= base + 1e-6)
    unmodified = ~parked_points & ~person & ~moving
    assert np.array_equal(confidence[unmodified], base[unmodified])
    bcd_passed = "bcd_regression" in report and all(
        max(c["after_confidence"]) <= config["moving_object_confidence"] + 1e-6
        for c in report["bcd_regression"].values())
    report["validation"] = {"input_sha256_checked": False, "geometry_motion_support_preserved": True,
                             "confidence_finite_and_bounded": True, "visibility_dynamic_stays_zero": True,
                             "motion_confirmed_confidence_capped": True, "no_target_unchanged_vs_base": True,
                             "new_scores_never_raise_confidence_above_visibility_base": True,
                             "all_ten_bcd_review_points_low_confidence": bcd_passed}
    (out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    (out / "tracks.json").write_text(json.dumps({"implementation": "local heuristic map-frame association",
        "tracks": tracks, "selected_detections": selected}, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k not in ("source_hashes", "scan_hashes", "scans", "config", "limitations", "parked_locations")}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
