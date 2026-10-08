# 车载 MID360 自身回波过滤

这个可选过滤器按 [Autoware Crop Box Filter](https://autowarefoundation.github.io/autoware_universe/main/sensing/autoware_pointcloud_preprocessor/docs/crop-box-filter/) 的车体回波处理方式工作：在雷达坐标系中设定一个固定盒形范围，删除盒内点，相当于其 `negative=true` 模式。当前建图链路使用 `livox_ros_driver2/CustomMsg`，因此这里使用针对该消息的轻量适配节点，而非直接运行 Autoware 接收 `PointCloud2` 的原节点。过滤位置在建图适配器和 FAST-LIO2 **之前**。原始录包及其话题保持不变；过滤器不会按车辆走过的轨迹擦除地图，也不会把未知区域标为可通行。

默认过滤范围如下，单位为米：

| 方向 | 最小值 | 最大值 |
| --- | ---: | ---: |
| X | -0.82 | -0.48 |
| Y | -0.18 | 0.18 |
| Z | -0.10 | 0.70 |

这个范围来自 2026 年 9 月车载 MID360 录包中雷达后方持续出现的点簇。抽取的 14 帧原始扫描全部在框内检出点；框内点数的第 10、50、90 百分位数分别为 57、65、72.7。它可能来自车后云台相机或安装件，但目前没有独立确认具体部件。默认范围只适用于这次安装位置，使用前应检查被删的点；进入该范围的真实外部物体也会被删除。

要与未过滤的地图比较，请把重新建图的结果写入**新的输出目录**：

```bash
./scripts/run_mid360_mapping.sh /path/to/bag /path/to/new-output \
  --lidar-topic /agt/sensors/lidar/custom \
  --imu-topic /agt/sensors/imu/data \
  --vehicle-return-filter
```

可以先加 `--dry-run` 查看启动命令，此时不会写入建图结果。需要调整范围时，使用 `--vehicle-return-box-min X Y Z` 和 `--vehicle-return-box-max X Y Z`。过滤器默认关闭，不能与另一套车体过滤选项 `--self-filter` 同时启用。

节点会保留其余点的顺序、字段和时间信息。与普通 Crop Box 删除规则相比，这里有一个扫描时间保护：如果一帧的最后一个点落在过滤框内，节点仍保留这个点，避免改变后续建图使用的扫描结束时间。已抽查的 14 帧均未触发这个例外。

先前位于 `confidence_map_trial_20260927/experiment_comparison/scheme_1_centerpoint_people_cars_self_vehicle/` 的试验，是对**已生成地图中的点**做离线排除，供效果预览。本过滤器处理的是**建图输入点云**。完整重跑 LIO/PGO 后，地图几何可能与离线预览不同，仍需单独比较。目前尚未完成这次全图重建。
