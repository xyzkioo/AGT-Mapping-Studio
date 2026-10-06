# agt_map_processing

完整接口契约：[接口说明](INTERFACE.md)。

Standalone ROS 2 package for offline map processing. No dependency on Studio,
Qt, OpenGL or a particular dataset. Studio invokes this package through the
generic `agt_map_runner` registry; see [runner contract](../agt_map_runner/INTERFACE.md).

## Build and run

```bash
colcon build --packages-select agt_map_processing
source install/setup.bash
ros2 run agt_map_processing process_map --package /path/to/map_package --output /path/to/new_result
```

From the repository toolbox, the equivalent entry is:

```bash
bash scripts/process_map.sh --map-package /path/to/map_package \
  --algorithm agt.offline_navigation --output /path/to/new_job
```

Outputs must use a new directory. Inputs are not overwritten. `--check-only`
checks required input files and the OctoMap executable without processing. `--profile`
can explicitly select an external profile. Cached CenterPoint detections are
postprocessed; this package does not run neural-network inference.

## Map data package

Map assets belong in an external data directory, not in this ROS package:

```text
map_package/
  map.pcd
  poses.txt
  poses_timed.txt
  patches/*.pcd
  metadata.yaml
  calibration.yaml
  manifest.yaml
  processing_profile.json
```

`processing_profile.json` contains `schema_version`, `package` and `assets`. `assets` names the `detections`, `source`, `baseline`, `parked`,
`evidence` and `tracks` files. Paths are relative to the
profile's directory; absolute paths are also accepted for migration. Input SHA256 validation is disabled; legacy checksum fields are ignored. Assets
may be in sibling data directories; move the complete data bundle together.

To migrate an existing profile into the data directory:

```bash
ros2 run agt_map_processing prepare_map --profile /path/to/old_profile.json \
  --output /path/to/map_package/processing_profile.json
```

This rebases paths, does not copy large scans, and refuses to overwrite an
existing profile. A new dataset still requires its own matching detection,
motion and parking assets. A raw XYZ PCD cannot supply the evidence needed by
this full pipeline.

## Results and integration

Results include filtered `map.pcd`, `navigation/map.yaml`, `navigation/map.pgm`,
processing reports, original pose/scan provenance and a rebased data profile.
The ground, filtering and eight-layer clearance rules are unchanged by this
separation. Original scans remain observation evidence; deleting points does
not make occluded space free.

Studio discovers `processing_profile.json` alongside the opened mapping source
and calls `agt_map_runner`, which launches the registered algorithm command.
Runner jobs contain `result.json`, `algorithm.log` and algorithm files under
`artifacts/`; direct backend calls write algorithm files into `--output` itself.
`AGT_MAP_PROFILE` may select
an explicit data profile for integration testing. Standalone 2D editing works
without running this package. No map-specific profile is installed with Studio
or this package.

Tests: `python3 test/test_algorithm_pipeline.py`. The Studio dataset smoke test
requires `AGT_MAP_PROFILE` and writes a new result directory.
`AGT_MAP_OUTPUT_ROOT` optionally chooses the Studio processing work directory.
Reprocessing a generated package uses the original observations referenced by
its data profile, rather than applying filters repeatedly to the filtered PCD.

## Data and algorithm settings

Map profiles describe data only: schema version, name, package directory, asset paths and optional coordinate-system metadata. Algorithm modes, defaults and legacy checksum fields are ignored and removed during profile migration. Defaults belong to this algorithm package. Per-run `--radius`, `--neighbors` and `--frames` override the algorithm defaults without editing map data or Studio.
