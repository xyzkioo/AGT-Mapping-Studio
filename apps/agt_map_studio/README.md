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
navigation-map generation through registered algorithms or the generic
converter. Relocalization and map publishing require the corresponding
external tools. Studio does not run live mapping or subscribe to ROS topics.

## 现有导航算法（2026-10-06）

第 3 步通过通用执行器发现兼容算法。只有一个可用项时自动选择，多个时显示列表；AGT_MAP_ALGORITHM 可以指定 ID。当前环境保留 agt.offline_navigation 旧流程和 agt.lizhi_map_pipeline 完整局部地面方案，投影小模块已取消注册。注册数量由安装环境决定，不写死在 Studio 中。

现有 MID360 数据包含 760,093 点和 359 个优化关键帧。运行前检查文件存在性，算法检查格式，不进行输入 SHA256 校验。CenterPoint 重新计算关联轨迹和点标记，不重新运行神经网络；另一张地图需要对应检测、运动和停车审核资产。

旧流程使用八层净空和每层至少三个关键帧的判定。完整局部地面方案将所有过滤标记应用到逐帧输入，重建 0.2 米 OctoMap，再投影地面上方 0.3–1.7 米到 0.1 米栅格，保留原障碍保护，不采用八层/帧数门槛。人工三维删除尚未接入现有注册算法：编辑后运行会明确拒绝，不再自动换成通用转换器。

任务写入会话工作目录（或 AGT_MAP_OUTPUT_ROOT）的 processing_runs/run_<时间>/。根目录有 result.json、algorithm.log，实际地图文件在 artifacts/。完成后按声明类型载入结果并保存 studio_session/studio_session.yaml，保留原发布名称和目标，不使用 artifacts 作为新地图 ID。

旧流程已复现：737,997 点；障碍 27,726、空闲 149,032、未知 928,720。完整局部地面方案已复现：737,997 点；障碍 30,619、空闲 219,553、未知 855,306。这些是现有数据的复现结果，不是动态去除或实车安全认证。

本地构建位于 .studio-build/、.studio-install/，scripts/map_studio.sh 加载该 overlay；已有窗口需重启。当前输入位于 /home/xyzkioo/datasets/lizhi_navigation/AGT_荔枝园巡检建图项目/01_输入/map_package。没有兼容注册且未要求使用注册算法的点云可以走通用转换器；指定算法不可用时明确报错。

执行器记录输入大小、修改时间、注册描述、实际参数和命令，用于过期判断，不计算输入内容 SHA256。历史结果缺少这些记录时沿用旧检查；保留大小和修改时间的内容变化无法由元数据检测。

File → Save 2D Map 保存可继续编辑的二维地图；Export Edited PGM 输出预览；第 4 步保存/应用发布用编辑；第 5 步才创建正式地图包。没有三维/二维编辑时，第 1/4 步无需执行，发布仍需有效重定位和导航数据。当前环境未安装 agt_map_manager，发布按钮会禁用并提示缺少外部工具。

## 独立二维编辑与保存（2026-10-06）

无需打开 PCD：用 **File → Open Occupancy Map** 打开 `map.yaml` 和它引用的 PGM；编辑后按 **Ctrl+S／Save 2D Map**，或 **Ctrl+Shift+S／Save 2D Map As**。首次保存选择父目录，Studio 新建 `edited_map_<时间>`，原图保持不变；之后 Ctrl+S 更新该编辑目录。输出包含 `map.pgm`、`map.yaml`、`map_refinement.yaml` 和 `keepout_zones.yaml`。禁行区独立于栅格，导航端仍需显式加载它。

重新打开输出的 `map.yaml` 会恢复匹配的编辑记录、禁行区和撤销/重做栈。保存采用临时目录写完整后替换，写入失败保留原输出；切换文件、退出前会提示未保存编辑，保存失败或取消不会继续。保存点云会话时也记录二维输出位置；恢复会话读取这个输出。源点云切换时清空旧二维图，避免不同数据混用。

当前二维读取支持 PGM、trinary 模式及零旋转角地图；其他模式或非零原点旋转会明确拒绝，避免静默失真。缺失外部工具的流程按钮会禁用，独立二维保存不需要这些工具。已有窗口需要重启才能使用新功能。

界面回归：启用上述 smoke 构建选项后，运行 `test_standalone_2d_workflow`，覆盖无 PCD 编辑、取消切换、保存重开、禁行区、撤销/重做恢复及写入失败保护。

## 独立算法包和通用注册（2026-10-06）

组合算法已迁移到 `processing/agt_map_processing`，Studio 不再编译或安装算法、OctoMap 构建程序和地图专用配置。通用执行器位于 `processing/agt_map_runner`，Studio 根据 `algorithm.yaml` 自动发现兼容算法，再读取统一的 `result.json`。工具箱可用 `bash scripts/process_map.sh --package <地图目录> --algorithm agt.offline_navigation --output <新目录>` 独立处理，结果也可以直接在 Studio 打开。算法使用说明见 [agt_map_processing](../../processing/agt_map_processing/README.md)，注册接口见 [agt_map_runner](../../processing/agt_map_runner/README.md)。
