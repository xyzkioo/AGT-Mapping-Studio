# agt_map_runner

Full integration contract: [接口说明](INTERFACE.md).

To unregister without deleting the algorithm program, delete its installed YAML
descriptor. Restore the descriptor from source or reinstall the package to register again. See
[取消注册与恢复](INTERFACE.md#取消注册与恢复) for commands and rebuild limitations.

`agt_map_runner` is the generic algorithm adapter used by Studio and the
toolbox. It discovers `algorithm.yaml` descriptors from installed packages,
checks the map data they require, starts the declared command, and consumes
the standard `result.json`.

```bash
ros2 run agt_map_runner map_runner --list-json --map-package /path/to/map_package
ros2 run agt_map_runner map_runner --map-package /path/to/map_package \
  --algorithm agt.offline_navigation --output /path/to/job --param radius=0.25
```

Algorithm defaults come from the descriptor. `--param` overrides a declared
parameter for one run. The map profile cannot override algorithm defaults.
`AGT_ALGORITHM_PATH` adds descriptor directories. Installed packages expose
descriptors under `share/<package>/algorithms/`; duplicate IDs are rejected.

Descriptors declare `id`, `name`, `required_inputs`, an argument-list
`command`, typed `parameters`, and typed `outputs`. Supported parameter types
are `int`, `float`, and `string`; supported output types currently include
`map_package`, `point_cloud`, and `occupancy_map`. Commands run directly as an
argument list; shell expansion is not performed. Every declared output must be
inside the job directory and exist after a successful command.

An algorithm package only needs to install its executable and descriptor:

```cmake
install(FILES algorithms/my_algorithm.yaml
  DESTINATION share/${PROJECT_NAME}/algorithms)
```

No Studio source change is needed. If multiple descriptors accept the opened
map, Studio uses `AGT_MAP_ALGORITHM` or presents the compatible choices.
