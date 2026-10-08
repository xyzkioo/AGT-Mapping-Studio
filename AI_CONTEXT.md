# AGT 地图项目交接

最后修改：2026-10-07

## 当前正式流程

荔枝园履带巡检建图。用户已认可并要求安装：原始XYZI/359帧扫描/优化位姿 → 缓存CenterPoint框重算跟踪和运动行人标签 → 车体过滤 → 局部地面高度参考 → 掩码回写配对扫描 → 原生OctoMap → 相对地面二维投影 → PGM/YAML。

正式源码和Studio安装入口均已切换native_octomap_local_ground_v2。半径滤波、每格两点/停车单点几何判障、四邻格扩张、坡度/高度极差阻塞、轨迹释放图、几何黑格强制覆盖全部移除；不是只跳过末尾覆盖。地面模型保持原有主平面＋0.5m粗格局部残差，仅输出ground_reference高度/有效掩码；树程序不接收地面或几何图。没有修改UI或添加算法开关。

二维0.1m、体素0.2m、射线25m、原点距离<0.5m排除，相对地面高度0.3–1.7m。带内有占据则黑，无占据且有任一已知空闲节点则白，无有效高度参考或无已知节点则灰；不要求八层全空闲或两/三帧净空，未验证整车净空。不要把拟合地面模型的角度条件误认为仍生成坡度障碍。

## 已验证结果

原始760093点不变，新正式PCD746359点；运动行人10540、车体3414、重合220，共删13734。树插入746356点，3点因原点距离未插入。图1199×922，原点[-90.9,-45.7]m，黑21534/白227746/灰856198。地面、删点掩码、PCD、树和二维图与认可的无半径直接投影预览一致，关键文件逐字节核对。实际Studio注册runner重跑通过，CMake构建和CTest三组七项通过，正式包740文件校验通过；报告补充主入口/配置/C++散列。

严格schema_version=2，assets仅detections/detection_binding；逐次SHA绑定地图/两份位姿/359扫描共362输入及检测框。只复用匹配的CenterPoint网络框，未重跑网络；跟踪、停车参考、地面、树和图重新计算。旧可见性、分数、人工停车审核、tracks、地面/树/地图均不导入。动态只删确认运动行人，运动汽车未删；3个停车/运动行人重合点仍被删，未同时改保护策略。

## 最新检查：长串黑点（2026-10-07）

用户截图已定位当前正式图，约X[-46.3,-37.1]、Y[-9.9,-0.7]m。对应17个原生占据体素、64个黑格、25个保留回波；逐帧重放359扫描，最终概率与当前树一致。每个体素仅一帧命中，来源256–281帧；命中后空闲射线更新均为0，此前空闲更新0–2次。最终占据概率0.50909/0.60870/0.7，超过原生0.5阈值，体素与局部高度带相交，所以直接投影为黑。部分点高于地面1.7m，但0.2m体素下部仍与带相交；不是新增高度规则。

来源BODY X中位-3.123m、范围[-3.875,-2.833]m，沿车辆路径随帧号递进；疑似随车移动的动态残影，具体物体身份未确认。当前自车框未覆盖这些点。原始行人框覆盖11点、当前接受框覆盖8点，确认运动框覆盖0点；当前person/moving/self标签全为0，未进入删点并集。不能把“没有动态标签”写成“已确认静态物体”，也不能把推测写成已确认跟车人。

证据：/home/xyzkioo/ros2_workspace/src/agt-lio-pgo-mapping/artifacts/black_chain_audit_20261007。report.json含逐体素统计，ray_events.csv含每帧hit/miss，occupied_nodes.csv为当前树节点，diagnosis.png和raw_context.png为位置/原始扫描预览。此次只诊断，未改算法、参数或正式地图；区别于更早针对几何合并图的中心环审计。

## 文件索引

- 地图项目：/home/xyzkioo/datasets/lizhi_navigation/AGT_荔枝园巡检建图项目。完整重跑选01_输入/map_package，查看02_当前方案/map_package/map.pcd或navigation/map.yaml，源码04_算法/agt_lizhi_map_pipeline。
- 实际工具仓库：/home/xyzkioo/ros2_workspace/src/agt-lio-pgo-mapping；scripts/map_studio.sh --package <原始包>；入口.studio-install/agt_lizhi_map_pipeline/lib/agt_lizhi_map_pipeline/process_map。pristine不是运行源码位置。
- 本次正式流程报告：/home/xyzkioo/ros2_workspace/src/agt-lio-pgo-mapping/docs/native_octomap_pipeline_20261007.md；同仓库artifacts/accepted_octomap_pipeline_20261007包含候选/旧源快照、完整差异、测试、重跑、verify_result.py及deployment.json。
- 切换前源码/安装/正式结果归档：地图项目07_历史资料/切换为原生OctoMap_20261007；原生OctoMap切换记录_20261007.json记录部署。更早清理档案继续保留。旧pipeline_code_audit报告仅代表修改前。

## 下一步与约束

先读长串黑点report.json和ray_events.csv，核对确认运动标签未形成的检测/跟踪原因，并确认实际物体身份；用户尚未要求针对这串点改算法，不扩大车体框、不自动加两帧占据门槛或半径过滤。继续核对细杆/路灯在地面有效区和高度带中的投影及Nav2膨胀影响；不要把减少的黑格全部称为假障碍。旧窗口/processing_runs可能仍显示旧图。根patches为原观测溯源，建树用filtered_observations/patches；注册算法仍拒绝当前手动3D删点，二维标注另存不回写PCD/树。

中心环此前493点/101簇，96簇仅一帧，距离BODY中位2.72m、都在车体框外；跟车人身份未确认，不扩大车体框。GridMap/DUFOMap/Removert、多帧/半径等保持实验，未经要求不得并入。工具与地图分开。地面无真值、车辆高度待实测、原包标定unavailable；原点偏移与MID360默认t_il一致，实际外参待核对。未连底盘做安全验收；历史Nav2域178/179可能旧图。两仓库其他未提交修改保留；本次不push。

## Studio XY联动（2026-10-08，侧会话）

用户要求开始实现二维／三维联动。仅修改xyzkioo仓库apps/agt_map_studio：打开完整地图包自动加载PCD与navigation/map.yaml（原map.pcd+manifest分支此前未加载二维）；发布包global_map布局同样支持。单独打开PCD／二维文件禁用联动，提示打开配套地图包。新增Ctrl+3双视图和二维“关联查看”，点击单格／拖框按XY关联所有高度的未删除点并定位三维，忽略Z窗口。三维选区对应二维格子红色高亮；仅显示覆盖、不改地图、不记编辑、不自动删点，Esc／Clear Selection恢复。没有对应点给出提示；右键二维平移支持。

本地.studio-build/agt_map_studio编译成功，9项相关CTest通过；新增test_xy_linked_inspection含实际红色绘制、保存仍为原黑格、双包布局和独立文件禁用。离屏检查不等于桌面OpenGL窗口验收，下一步重启Studio、打开完整包实测交互。仓库.studio-install的map_viewer已链接到该构建，scripts/map_studio.sh使用此overlay。用法见apps/agt_map_studio/README.md“二维／三维XY联动”。本次未改算法／地图数据／pristine，未commit/push；其他会话原有修改保留。

Studio联动补修（2026-10-08）：用户反馈对照预览左键不能转视角。show_linked_view现在自动set_mode_navigate，避免沿用Select/Delete把拖动当框选；仍保留选区和二维红色。新增自动测试从Select进入预览、实际发送左键拖动并核对相机位置变化／高亮不变；编译和两项相关CTest通过。当前已开进程需重启使用补修，旧窗口可先点Navigate(N)。

Studio选区清除（2026-10-08，侧会话）：按用户要求新增“清除当前选区”，直接将联动红色格子中的二维障碍改为空闲并清除高亮，不需重复拖框；空闲／未知不变，不删三维点云。采用稀疏格子编辑，不清除未选中的间隙；支持撤销、重做、保存及历史恢复，erase_selected_cells记录实际格子中心与分辨率，发布patch按单格导出。编译及5项相关CTest通过，包括稀疏选区、历史／patch恢复和实际按钮操作；既有窗口需重启。算法和地图数据未修改，未commit/push。

Studio清除入口合并（2026-10-08，侧会话）：按用户最新要求移除单独“清除当前选区”按钮，统一到Erase rect。有联动红色选区时点击直接清除其中的二维障碍并取消高亮；无选区时进入原拖框模式。稀疏编辑、未知保留、撤销／重做和保存逻辑不变，不删除点云。新增回归验证两个分支和旧按钮已移除；编译与5项相关CTest通过。既有Studio进程仍需重启使用。

Studio界面语言（2026-10-08，侧会话）：用户改为要求统一英文。新增联动菜单为2D / 3D Linked View，关联工具为Linked Inspect；配套地图包提示、选区计数、清除结果／失败和Erase rect说明已改英文。原菜单和流程面板原本为英文，技术名称和文件格式不变。此项仅修改界面文案，不改变算法或操作逻辑。

Studio细杆补标（2026-10-08，侧会话）：按用户要求新增英文工具Mark obstacle，在二维单击补标遗漏细杆；Width (m)控制方形范围，向上取整为整格且至少一格，可把空闲／未知设为障碍。左键拖动不连续涂画、右键平移保留；点击清掉联动高亮以显示黑色格子，不删点云。复用fill_occupied_polygon及已有历史／发布patch，支持撤销、重做、保存和重开。编译及4项相关CTest通过；新增鼠标点击回归验证低于分辨率的宽度、空闲／未知补标、点云未改变、保存／撤销／重做／重开。既有窗口需重启使用，未修改算法和原地图数据。
