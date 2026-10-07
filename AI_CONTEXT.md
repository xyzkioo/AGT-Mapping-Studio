# AGT 地图项目交接

最后修改：2026-10-07

## 项目与当前状态

为荔枝园履带巡检车生成点云及导航栅格，使用现有 AGT Map Studio。主方案为 **OctoMap＋局部地面**：三维体素0.2m、二维格0.1m、相对地面高度0.3～1.7m、射线范围25m；不要求八层全部空闲或两/三帧净空支持。当前参考PCD为737,997点，栅格黑30,619、白219,553、灰855,306。

完整流程含缓存CenterPoint后处理、车体过滤及旧障碍候选半径过滤（0.2m、至少5邻点）；不能称为纯OctoMap或完全无孤立过滤。新增三维孤立过滤仅实验。局部地面层的“每格至少两个障碍点”是项目附加规则，同一帧也能满足；不是OctoMap原生规则。

## 近期决策与实验

- 10月6日：时序空闲融合只清掉一簇8格，用户认为收益不足；**源码及Studio运行文件已撤回**，证据见 `artifacts/temporal_obstacle_fusion_20261006_v2/reverted.json`。
- 两帧障碍确认仅实验、未接入：0.1m XY格至少两帧命中，车辆豁免、地形保留、地面估计不改。黑格减少360：172转白、188转灰；8个小簇完全空闲。左墙附近21格变化，不能将减少的都判为噪声；PCD未删点。
- Removert是作者核心思路适配实验，非完整官方复现，未采用。DUFOMap等历史结果不代表当前生产方案。
- 10月7日：用户认为现场地面差不多高。直接统计低处回波仍显示地图世界Z随位置变化；低处回波不是人工确认地面。实际坡度、世界坐标倾斜、姿态/外参或轨迹误差尚未区分，不能断言现场右上更高。
- 已集中整理191份报告/说明、197份附件；历史文本可能包含旧路径、旧建议，当前状态以此交接和总结为准。

- 10月7日：实际工作区Studio新增 `Free rect`，左键拖矩形、松开后将框内障碍/未知设为空闲；保留多边形与原仅清障碍框选，复用撤销、重做和导出。`map_viewer` 已重新编译，旧窗口需重新启动。修改在实际工作区，pristine未同步工具源码。

## 文件索引

下面路径均为绝对路径，勿使用已迁移的 `/home/xyzkioo/下载/v003-indexed`。

- 工具与实际实验工作区：`/home/xyzkioo/ros2_workspace/src/agt-lio-pgo-mapping`。
- 当前会话检出：`/home/xyzkioo/ros2_workspace/src/agt-lio-pgo-mapping-pristine`；两份检出各有未提交修改，禁止覆盖或混入本次修改。
- 地图项目：`/home/xyzkioo/datasets/lizhi_navigation/AGT_荔枝园巡检建图项目`。Studio打开完整输入应选择其 `01_输入/map_package`，而非项目根目录；参考结果在 `02_当前方案`。
- **当前完整算法源码**：地图项目下 `04_算法/agt_lizhi_map_pipeline`；算法ID `agt.lizhi_map_pipeline`。仓库旧 `processing/agt_map_processing` 不是当前完整算法。
- Studio运行算法：实际工作区 `.studio-install/agt_lizhi_map_pipeline/lib/agt_lizhi_map_pipeline`；源码更新不等于安装生效。
- 启动入口：实际工作区 `scripts/map_studio.sh --package <输入地图包>`，或 `--pcd <文件>` / `--map <map.yaml>`。
- 总结及报告索引：地图项目下 `05_报告汇总/00_总结报告.md`、`05_报告汇总/README.md`、`05_报告汇总/来源索引.json`。报告是副本，原资料未删。
- 最新对比：实际工作区 `artifacts/two_frame_obstacle_trial_20261006/{report.json,comparison.png,center_detail.png,navigation/map.yaml}`；地面检查在 `artifacts/ground_height_review_20261007/{report.json,ground_height_comparison.png,*_low_returns.csv}`。

## 下一步与约束

接手先读总结报告，再核对当前算法与实验报告。若继续查高度：人工选取左右明确地面小片，比较实测Z/法向，再核验世界坐标与重力方向；别直接调高下限追求更多白格。车辆实际高度尚未实测确认，0.3～1.7m不是安全认证范围。

工具与地图资料分开；用户不需要新增UI或单算法开关。两帧方案未获生产接入决定，不要自动安装。地图项目在本会话可写根目录之外，修改需工具权限批准。已有未提交代码属于其他工作，不清理、不覆盖。报告未做实车导航验证；用户只授权删除无依赖重复副本，不能扩展删除范围。
