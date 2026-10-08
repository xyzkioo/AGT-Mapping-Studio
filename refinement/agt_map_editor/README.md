# agt_map_editor

RViz interactive editor for Phase 2 refinement rules.

The recommended workflow is one command. It opens RViz, loads the source
point cloud, and automatically exports the derived package whenever `SAVE` is
clicked:

```bash
~/ros2_ws/src/agt_mapping_framework/scripts/edit_map.sh \
  ~/ros2_ws/experiments/artifacts/bunker_mid360_v003/map_package \
  ~/ros2_ws/experiments/artifacts/bunker_mid360_v003/refined_map_package
```

The original source package is never modified. The output package may be
regenerated in place because it is a derived artifact.

Keyboard controls are available when the editor is started from a terminal:

```text
W / S       move the active box along Y
A / D       move the active box along X
Q / E       move the active box along Z
R / F       enlarge / shrink the active box in X and Y
Delete      remove points inside the active box from the live display
Ctrl+S      save refinement.yaml and export the refined package
```

The active box is a preview until `Delete` is pressed. The display updates
immediately after deletion; the source PCD is unchanged until the save/export
step. Keep focus in the terminal running `edit_map.sh` while using the keys.

Saved deletion boxes are green. The active box is amber and is rendered as a
full 3D cuboid, so its X, Y, and Z extents are visible separately.

For manual launch with an existing rule file:

```bash
ros2 launch agt_map_editor map_refinement_editor.launch.py \
  refinement_file:=/path/to/refinement.yaml \
  map_pcd:=/path/to/map_package/map.pcd
```

The editor displays at most 100,000 sampled points by default for responsive
RViz interaction. The source PCD remains full resolution and is used for the
export. Adjust with `display_max_points:=50000` or `200000` when needed.

In RViz, add a `PointCloud2` display with topic
`/map_refinement_editor/map_cloud` and an `InteractiveMarkers` display for topic
`/map_refinement_editor/update`. Drag polygon vertices or the two box corner
handles in the `map` frame. Click `ADD BOX` or `ADD POLYGON` to create a new
operation, then drag its handles. Click the visible `SAVE` marker, or call the
`/map_refinement_editor/save_refinement` service, to write the YAML file.

This MVP edits existing operations. It intentionally does not alter the
source PCD. The one-command launcher calls `agt_map_refinement_core` after
saving to publish a new refined package containing the edited PCD, PGM, YAML,
metadata, manifest, and checksums.

## 二维栅格手动编辑（PGM/YAML）

独立桌面工具，直接编辑 Nav2 `trinary` 地图，无需 ROS 启动或 colcon 构建。

```bash
./scripts/edit_grid_map.sh /path/to/map.yaml
# 或先启动，再点击“打开地图”
./scripts/edit_grid_map.sh
# colcon 构建后的入口
ros2 run agt_map_editor grid_map_editor /path/to/map.yaml
```

依赖：Python 3、Tk、NumPy、Pillow（含 ImageTk）、PyYAML。Ubuntu 对应包
`python3-tk python3-numpy python3-pil python3-pil.imagetk python3-yaml`。

操作（简化版）：

1. 用“多边形框”左键逐点添加顶点，Enter 或“闭合画框”完成；
   “自由画框”按住左键沿边界描一圈，松开闭合；“矩形框”拖出矩形，松开完成。
   **画框只选中，不修改地图**；亮蓝色边框指示选区，内部不加色块。
2. 点击“设为空闲”“设为未知”或“设为障碍”，整个选区立即变为该状态，
   不区分修改前是黑、灰还是白。框内直接显示白色空闲、灰色未知、黑色障碍。
3. 不需要额外应用按钮。未选中区域时点击状态不会修改地图；先画框再点击状态。
4. 点击其他位置开始画新框，Esc 清除选区（保留已经修改的格子）；
   Ctrl+Z 撤销，Ctrl+Y 重做，Ctrl+S 或“另存新目录”导出。
   未闭合的框需先完成或取消。界面显示选区实际黑/灰/白格数。

滚轮以鼠标位置缩放；右键拖动平移；“适应窗口”恢复全图。
不使用橙色修改遮罩或蓝色区域填充，避免遮住实际状态。
鼠标下方显示地图坐标（米）；有选区时保留选区状态提示。
历史占用限制约 128 MiB，超过时丢弃最旧撤销项（最近一次始终保留）。
可以填写修改依据/备注，将随操作写入记录。尚未导出就关闭或换图时会提醒。

“另存新目录”填写**不存在的目录名**，输出 `map.pgm`、`map.yaml`、`edits.json`、
`changed_cells.png`（白色为相对输入改变的格子）。禁止覆盖已有目录，包括原地图目录。
日志记录来源文件及 SHA256、修改时间、来源过滤、目标状态、备注、格数和像素范围；
仅包含当前有效操作，不包含被撤销的操作。导出发生错误时目录可能不完整，请换新目录重试。

分辨率、原点（含 yaw）、尺寸和其他 YAML 参数保留；未修改像素保持原值。
支持 8 位灰度、无透明度的 `trinary` 地图；拒绝 `scale/raw` 和彩色地图。
像素坐标从左上角计数，世界坐标按格子中心、原点旋转与上下翻转换算。

编辑只影响另存的二维导航地图，不改变 PCD、OctoMap 或输入 bag。
应根据现场核查等依据修改未知或障碍区域；人工填白并非实车导航验证。

测试：

```bash
PYTHONPATH=refinement/agt_map_editor python3 -m pytest refinement/agt_map_editor/test -q
```
