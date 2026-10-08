# FAST-LIVO 离线可见性清理（可选）

`filter_livo_visibility` 在 FAST-LIVO 保存地图后，另外生成一份供检查的 PCD。原始 `localization_map.pcd` 不会被修改；该命令不改变在线里程计或建图过程。

核心的多帧可见性判断来自 [dynamic-3d-object-removal](https://github.com/rsasaki0109/dynamic-3d-object-removal) 的 `clean_map_by_visibility`，固定来源提交 `0a640ba198a00d554a8538a0c7a1d955d6d85356`。vendored 源文件在 `agt_map_refinement_core/opensource_visibility.py`，MIT 许可见本包根目录 `UPSTREAM_VISIBILITY_LICENSE`。本工程在其候选删除结果上加了路径距离及局部离地高度限制。

## 输入和输出

输入是 **同一次 FAST-LIVO 运行、同一地图坐标系** 的二进制 float32 PCD、`stamp_ns,x,y,z` 位姿 CSV，以及包含地图坐标系下 `sensor_msgs/msg/PointCloud2` 注册点云的 ROS2 sqlite3 bag。注册点云的每帧时间戳与最近位姿须相差不超过 `--max-pose-gap`。如果注册点云只是雷达局部坐标，须先转换到地图坐标系。

在已构建并 source 的 ROS2 工作区执行：

```bash
ros2 run agt_map_refinement_core filter_livo_visibility \
  --map-pcd /path/to/localization_map.pcd \
  --bag /path/to/rosbag_directory \
  --poses-csv /path/to/poses.csv \
  --output-dir /path/to/new_review_directory \
  --region-xy X_MIN X_MAX Y_MIN Y_MAX
```

默认扫描话题为 `/agt/commissioning/mapping/registered_points`，可用 `--registered-topic` 修改。`--region-xy` 必填，以便限制处理范围。默认每 40 帧取一帧，至少 4 帧可见性穿透、至多 2 帧表面确认，角分辨率 2.5°；路径半径 1.5 m，离局部最低点 1.0–2.1 m。命令支持调整这些参数，详见 `--help`。

新目录生成 `localization_map_cleaned.pcd`、`removed_points.pcd`、`report.json`。若这些文件已存在，命令会拒绝覆盖。清理后先比较地图结构、底层地面和目标弧线，再决定是否把新地图用于后续定位。不要直接替换原图。

## 本次 Bunker / Mid360 录包

在 `bunker_mid360_mapping_20260901_211105` 的同次回放原图上，区域 `-63 -10 -18 21`、67 帧注册点云，开源算法提出删除 9686 点；路径和高度限制后删除 7874 点。手工标注的中央弧线从 4091 点降为 216 点，路径 2 m 外删除 0 点，z≤-1.2 m 删除 0 点。这个统计只能说明该录包的可见效果，仍需检查路径附近真实结构是否被误删。
