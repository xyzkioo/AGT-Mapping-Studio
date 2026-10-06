# agt_map_studio

Offline point-cloud and 2D occupancy-map editor for the AGT mapping framework.

## Features

- Load ASCII, binary and binary-compressed PCD files through PCL.
- Keep the complete `PCLPointCloud2` field set in memory, including `intensity` when present.
- Render XYZ points with Qt5/OpenGL.
- Color points by Z height by default: blue is low and red is high.
- WASDQE camera movement; hold Shift for `fast_speed`.
- Left mouse orbit, right mouse pan, wheel zoom.
- Point-size, background, axis, reset-camera and FPS controls.
- `View -> Color by Z Height` colors by elevation. `View -> Color by Scalar Field...` selects any numeric PCD field and shows its value range; high values are red and non-finite values are gray.
- Save camera/view state to a separate YAML file.
- Non-destructive Round 2A editing: box selection, point-state colors, DeleteBox
  undo/redo and clean-map export.
- `Tools -> Generate Occupancy Map` runs the offline PCD-to-PGM baseline with
  configurable resolution and Z filtering, then shows a 2D preview.
- `File -> Open Occupancy Map` loads a Nav2 `map.yaml` and its relative P2/P5
  PGM into the independent 2D editor.
- Round 2B-2B refinement editor: erase rectangles, obstacle lines, forbidden
  polygons, sparse undo/redo and traceable refinement export.

This is an offline viewer. It does not create a ROS node, subscribe to topics,
modify a `map_package`, or participate in FAST-LIO2/PGO/mapping execution.

## Build

The current environment has Qt 5.15.3 rather than Qt6, so the CMake file uses
Qt5 automatically and does not install anything.

```bash
cd /home/yangxuan/ros2_ws
colcon build --base-paths src/agt_mapping_framework/apps/agt_map_studio \
  --packages-select agt_map_studio
source install/setup.bash
```

## Run

```bash
ros2 run agt_map_studio map_viewer \
  --pcd /home/yangxuan/ros2_ws/experiments/artifacts/output/mid360_20260901_205036/map_package/map.pcd
```

To display confidence or another scalar field produced by an external algorithm, store it as a numeric PCD field alongside `x y z`, then select it in the View menu or pass it on startup:

```bash
ros2 run agt_map_studio map_viewer \
  --pcd /path/to/scored_map.pcd --color-field confidence --color-min 0 --color-max 1
```

Without explicit limits, the viewer uses the finite minimum and maximum in that PCD. It only displays the field; scoring and point removal remain the external algorithm's responsibility.

The executable also accepts a positional path:

```bash
ros2 run agt_map_studio map_viewer /path/to/map.pcd
```

`File -> Save View` writes camera state only; it never writes back to the
source map package.

## Interaction

| input | action |
| --- | --- |
| Left mouse drag | Orbit/rotate camera |
| Right mouse drag | Pan camera |
| Mouse wheel | Zoom |
| W / Down arrow | Move forward/backward |
| A/D | Move left/right |
| Q/E | Move down/up |
| Shift | Fast movement |
| R | Reset camera |
| N/S/D | Navigate/Select/Delete mode |
| Delete | Delete selected points in Delete mode; deleted points remain red |
| Ctrl+Z / Ctrl+Y | Undo / redo the latest delete-box operation |
| 0/1/2 | Isometric / front / top view |
| `+/-` | Increase/decrease point size |

In Select or Delete mode, drag the left mouse button to create an axis-aligned
screen selection. Selected points are yellow. In Delete mode press Delete to
mark them red; the source PCD is never erased or overwritten.

The `Help -> Controls` menu shows the same shortcuts inside the viewer.

`File -> Export Clean Map` asks for a parent directory and creates a new
`clean_map/` directory containing:

- `map.pcd`: only non-DELETED points, with the original PCL fields preserved;
- `edit_history.yaml`: delete-box operations and undo state;
- `metadata.yaml`: source, point counts and exported field names.

`Tools -> Generate Occupancy Map` uses the installed
`agt_pcd2grid_exporter/config/projection.yaml`, displays the effective
parameters for confirmation, and creates a separate `navigation_map/`
directory containing Nav2-compatible `map.pgm`, `map.yaml`, the effective
projection parameters and metadata. It never overwrites the source PCD.

In the 2D occupancy view, left-drag pans, the wheel zooms around the cursor,
`R` resets to a centered 1:1 view, and `F` fits the whole map. The status bar
shows the grid pixel, lower-left grid coordinate and world XY coordinate under
the cursor. Use the occupancy toolbar for the refinement operations described
below.

## Occupancy refinement

The occupancy toolbar provides four modes:

- `View`: pan and zoom only;
- `Erase`: drag a rectangle, confirm, and convert occupied cells to free;
- `Obstacle`: drag a line; `Width (m)` controls its rasterized width and free
  cells become occupied;
- `Forbidden`: click polygon vertices and double-click to finish; the zone is
  stored as a translucent red layer without changing occupancy values. `Esc`
  cancels the in-progress polygon.

`Ctrl+Z` and `Ctrl+Y` undo/redo 2D operations while the 2D page is active.
`File -> Save Map Refinement` writes `map_refinement.yaml`. `File -> Export
Navigation Map` writes a new `navigation_map/` directory containing `map.pgm`,
Nav2-compatible `map.yaml`, `map_refinement.yaml` and `metadata.yaml`. The
original base PGM is never modified.

## Current scope

Studio supports offline point-cloud editing, standalone 2D map editing and
navigation-map generation. Compatible mapping packages can use the fixed
CenterPoint postprocessing, vehicle body filtering, isolated-point filtering
and OctoMap pipeline. Relocalization and map publishing require the corresponding
external tools. Studio does not run live mapping or subscribe to ROS topics.

## 现有导航入口的固定组合处理（2026-10-05）

界面和按钮保持原样。打开原始完整 `map_package` 后，原来的 **3. Generate Navigation Layers**（或 Tools 中同名入口）现在运行固定组合：CenterPoint 检测缓存后处理 → 逐帧车体过滤 → 地面分离后的二维半径孤立点过滤 → OctoMap 八层净空、每层至少三帧空闲融合。没有单独算法开关。

当前预设只对应 `algorithms/current_map_profile.json` 中的现有 MID360 数据：760,093 点、359 个优化关键帧。它校验地图、位姿、逐帧点云及检测资产的 SHA256；另一张地图需要配套检测资产，不能套用此缓存。CenterPoint 重新计算去重、关联轨迹和点级标记，但**不重新运行神经网络推理**。旧运动证据和停车车审核证据继续作为共同基准；四步不是四个完全独立算法的消融试验。

固定参数沿用旧实验：0.1 m 二维格、地面容差 0.14 m、障碍高度 0.22–2.2 m、车体 BODY/IMU 盒 `[-0.85,-0.4] × [-0.2,0.2] × [-0.2,1.0] m`、半径 0.20 m/至少 5 点（含自身）、0.2 m OctoMap、地面上方 0.3–1.7 m 八层、每层至少三个关键帧。此固定组合不使用面板里的通用转换器参数；这些参数仍供普通 PCD/手工细化后的转换路径使用。OctoMap 只补未知→空闲，已有障碍保留；射线证据使用原始优化帧，不把删点后的射线延长为空闲。

结果写入现有实验目录的 `studio_pipeline_runs/run_<时间>/`，包括真正过滤后的 `map.pcd`、`removed.pcd`、`annotated.pcd`、`point_masks.npz`、`navigation/map.yaml`、`octomap/keyframes.bt`、`report.json`，以及复制的原关键帧/位姿、派生 manifest 和校验和。完成后自动载入新点云和二维图，可以用原 3D/2D 切换查看。PCD 排除与二维障碍证据相同的动态、车体和半径候选点。原文件不覆盖。

已验证完整链：737,997 个保留点；二维障碍 27,726、空闲 149,032、未知 928,720，逐格复现旧三帧图。这个数值是现有数据上的复现，不代表动态物体已经全部去干净。

本次本地构建在仓库 `.studio-build/`、`.studio-install/`；`scripts/map_studio.sh` 优先加载此 overlay。旧窗口需重新启动才能运行新后端。完整输入默认位置：`/home/xyzkioo/Documents/Codex/2026-09-22/new-chat-2/work/ab_results/improved/map_package`。从单独 PCD 或不含关键帧的成品地图包打开，仍使用原通用转换器；不会声称已执行完整四步。

回归检查：`/usr/bin/python3 apps/agt_map_studio/test/test_algorithm_pipeline.py`。构建时增加 `-DSTUDIO_ALGORITHM_SMOKE=ON` 可启用实际界面原导航动作的端到端测试（使用当前配套资产，输出独立目录，约 20 秒）。

## 独立二维编辑与保存（2026-10-06）

无需打开 PCD：用 **File → Open Occupancy Map** 打开 `map.yaml` 和它引用的 PGM；编辑后按 **Ctrl+S／Save 2D Map**，或 **Ctrl+Shift+S／Save 2D Map As**。首次保存选择父目录，Studio 新建 `edited_map_<时间>`，原图保持不变；之后 Ctrl+S 更新该编辑目录。输出包含 `map.pgm`、`map.yaml`、`map_refinement.yaml` 和 `keepout_zones.yaml`。禁行区独立于栅格，导航端仍需显式加载它。

重新打开输出的 `map.yaml` 会恢复匹配的编辑记录、禁行区和撤销/重做栈。保存采用临时目录写完整后替换，写入失败保留原输出；切换文件、退出前会提示未保存编辑，保存失败或取消不会继续。保存点云会话时也记录二维输出位置；恢复会话读取这个输出。源点云切换时清空旧二维图，避免不同数据混用。

当前二维读取支持 PGM、trinary 模式及零旋转角地图；其他模式或非零原点旋转会明确拒绝，避免静默失真。缺失外部工具的流程按钮会禁用，独立二维保存不需要这些工具。已有窗口需要重启才能使用新功能。

界面回归：启用上述 smoke 构建选项后，运行 `test_standalone_2d_workflow`，覆盖无 PCD 编辑、取消切换、保存重开、禁行区、撤销/重做恢复及写入失败保护。
