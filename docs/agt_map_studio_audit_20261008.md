**AGT Map Studio 审查报告 · 2026-10-08**

以下为修复前的审查基线。第一批 F01–F05 及关联 F11 的实现和最新验证见 [修复记录](agt_map_studio_fixes_20261008.md)。

优先修复保存/恢复、删除规则和异步结果的一致性，再优化响应速度与界面。此次发现 11 类已复现问题，其中 5 类按 P1 排序、6 类按 P2 排序；另列出尚未端到端验证的代码路径风险。P1 表示会造成编辑丢失、输出语义改变或错误接受旧结果，P2 表示条件性错误或交互/状态缺陷；这是本次修复建议的优先级。

实际运行仓库为 当前工作仓库，不是聊天当前目录 `agt-lio-pgo-mapping-pristine`。`scripts/map_studio.sh` 加载 `.studio-install`，安装入口链接到 `.studio-build/agt_map_studio/map_viewer`。审查覆盖 Studio 的 40 个源文件、7928 行，以及 8 个测试源文件、CMake/package/README、启动脚本和直接调用的 `agt_map_runner`。算法内部的检测、跟踪、OctoMap 准确性不在这次 Studio 审查的验证范围内。

审查阶段只增加本报告和审查复现材料，未改产品源码、算法、正式地图或已有未提交修改。代码版本、源文件 SHA256 和验证范围保存在 [manifest.json](../artifacts/agt_map_studio_audit_20261008/manifest.json)。

**已复现的问题**

1. **F01 · P1：三维会话保存、恢复没有闭环。**

   触发：打开 PCD，删一个点，点击 Save Studio Session，重新打开保存的会话。复现中 `Deleted: 1` 变成 `Deleted: 0`，而且首次保存没有写 `refinement.yaml`。即使先执行 Apply 3D Refinement 后再恢复，界面仍加载原始 PCD，删除状态为 0。

   Save Studio Session 只保存 `WorkflowSession` 的路径/指纹；`open_session()` 调用 `open_pcd()` 后没有恢复 SelectionManager 的点状态、规则、历史和撤销栈。退出保护中的 Save 会写规则，但恢复入口同样没有读取它。加载二维图时的 `sync_edit_fingerprints()` 还会用空 SelectionManager 覆盖恢复的三维指纹。仅把“退出前有保存提示”当作三维编辑已经安全持久化是不成立的。

   位置：[保存入口](../apps/agt_map_studio/src/ui/MainWindow.cpp:152)、[恢复入口](../apps/agt_map_studio/src/ui/MainWindow.cpp:913)、[退出保存](../apps/agt_map_studio/src/ui/MainWindow.cpp:1198)。建议统一会话保存入口，持久化原始点索引、删除命令及撤销/重做，并在所有内容验证成功后一次性替换当前文档。

2. **F02 · P1：三维离散选择被导出成更大的删除盒子。**

   触发：三点位于 X=0、1、2；先选中中间点，再反选并删除。界面实际删除 0 和 2 共 2 点；导出的 `remove_box` 从 0 到 2，后端规则会删除包含中间点在内的 3 点。

   `invert_selection()` 清空规则几何；`rebuild_selection_box_from_points()` 再用整个选中集合的 AABB 代替真实集合。屏幕矩形也采用相似的 AABB 导出策略。斜视多边形则把屏幕选区投到最低 Z 平面，代码承认它是近似；注释所说的 AABB 证据并没有随 `remove_polygon` 序列化。因此“界面红点”和“规则执行实际删点”缺少一致性保证。

   位置：[反选](../apps/agt_map_studio/src/selection/SelectionManager.cpp:94)、[AABB 重建](../apps/agt_map_studio/src/viewer/PointCloudViewer.cpp:773)、[多边形投影](../apps/agt_map_studio/src/viewer/PointCloudViewer.cpp:804)。建议支持精确点索引/掩码，或让界面按将要导出的同一规则计算选择。改变规则格式还需要同步后端，不能仅改显示层。

3. **F03 · P1：撤销删除后再按 Delete，原几何规则丢失。**

   触发：选择 X=10 的点并以球形删除，Undo，然后直接 Delete。界面仍删除这一个点；新导出规则却变成 `remove_box`，min/max 默认均在原点。

   `delete_selected()` 最后清空选择几何；`undo()` 只恢复点状态和索引，没有恢复该命令的 geometry。用户看到恢复的黄色点会自然认为再次 Delete 等价于原操作，实际规则却不同。

   位置：[undo()](../apps/agt_map_studio/src/selection/SelectionManager.cpp:135)、[rebuild_selection_from_statuses()](../apps/agt_map_studio/src/selection/SelectionManager.cpp:218)。建议连同选择几何恢复，且导出前拒绝无效盒子；加入 Delete→Undo→Delete→规则回放回归。

4. **F04 · P1：异步步骤把运行中的新编辑标成已处理。**

   使用临时假工具隔离复现 Studio 的调度逻辑：启动 Apply 3D Refinement 时有 1 条删除规则；工具运行期间再删除一个点；完成后界面显示 Deleted=2，传给工具的 YAML 只有 1 条操作，Refine 状态却是 Fresh。

   完成回调中的 `mark_done()` 读取完成时的当前指纹，未保存启动时的输入快照；运行时三维编辑仍可用。通用转换、重定位、外部 patch 回调也采用这类模式。注册算法入口已有输入变化保护，但没有覆盖所有执行路径。

   位置：[Refine 回调](../apps/agt_map_studio/src/ui/MainWindow.cpp:1705)、[mark_done()](../apps/agt_map_studio/src/workflow/WorkflowSession.cpp:227)、[注册算法已有保护](../apps/agt_map_studio/src/ui/NavigationAlgorithms.cpp:90)。建议统一 RunContext，保存 source/edit/parameter revision；回调比较快照，过时结果另存并保留当前编辑，或在运行期间禁用相关修改。

5. **F05 · P1：二维保存会改变未知栅格的含义。**

   输入三个格子 `[occupied, free, unknown]`，导出再用实际 MapYamlLoader 读取：默认配置得到 `[100,0,-1]`；`negate=1` 得到 `[100,0,100]`；`negate=0, free_thresh=0.4` 得到 `[100,0,0]`。未知分别变成障碍和空闲。该问题没有在默认 `negate=0/free_thresh=0.196/occupied_thresh=0.65` 的对照中出现，不能据此认定当前正式地图已经受影响。

   写入端固定使用 0/254/205，同时继承输入阈值；negate 只反转已知格。205 的概率并不适用于所有阈值/negate 组合。

   位置：[栅格写入](../apps/agt_map_studio/src/occupancy/RefinementModel.cpp:383)。建议选择与输出阈值相容的三值编码，或规范化输出 negate/阈值；写入临时目录后重新加载并逐格比对，成功后再提交。

6. **F06 · P2：通用结果的 Fresh 判断没有检查文件内容。**

   复现：为通用 Navigation 记录 map.pgm 的 SHA256，随后改写 map.pgm；`state(Navigation)` 仍返回 Fresh。普通分支只检查记录路径存在和内存指纹是否一致，记录的 output_sha256 没有拿来核对。注册算法分支有部分输出/输入校验，两条路径的可信度不同。`load()` 也直接接受保存的源 PCD 指纹，恢复时缺少源文件变化检查。

   位置：[state()](../apps/agt_map_studio/src/workflow/WorkflowSession.cpp:135)、[普通分支](../apps/agt_map_studio/src/workflow/WorkflowSession.cpp:165)、[源指纹恢复](../apps/agt_map_studio/src/workflow/WorkflowSession.cpp:394)。建议统一必需资产的存在性和 size/mtime revision 检查，并在有变化时核对已有输出散列；同时校验 map.yaml 几何。没有必要重新引入所有算法输入的强制 SHA256 准入。

7. **F07 · P2：二维多边形按格角点采样，和“格中心”语义不符。**

   复现：1 米栅格、三角形 `[(0,0),(3,0),(0,2)]`。格 `(2,0)` 的中心 `(2.5,0.5)` 在三角形外，实际却被填成 occupied。FillPolygonCommand 中名为 center 的坐标来自 `pixel_to_world()`，该函数返回角点；DrawObstacleCommand 也有相同调用。现有名为 FillsCellCentersInsidePolygon 的测试没有检查能区分中心与角点的边缘格，因此通过了。

   位置：[角点定义](../apps/agt_map_studio/src/occupancy/GridMap.cpp:46)、[多边形采样](../apps/agt_map_studio/src/occupancy/commands/FillPolygonCommand.cpp:61)。建议分开 `cell_corner_world` / `cell_center_world`，明确中心包含还是与格相交的栅格化约定，再统一线、多边形、稀疏 patch 和预览。改变约定会影响旧编辑的重放，需要兼容策略。

8. **F08 · P2：带 CRLF 头的 P5 被静默错读。**

   复现：头为 `P5\r\n2 1\r\n255\r\n`，像素为 0、254，读出占据值 100、100，第二格本应为空闲。读取 max_value 的 token 消耗 CR 后，剩下 LF 被当作第一个像素，最后一个真实像素被丢下；长度检查仍通过。

   位置：[token 分隔](../apps/agt_map_studio/src/occupancy/MapYamlLoader.cpp:28)、[二进制读取](../apps/agt_map_studio/src/occupancy/MapYamlLoader.cpp:92)。建议明确处理二进制头的换行边界，覆盖 LF、CRLF、16 位和截断输入；不要直接跳过任意空白字节，因为像素本身可能等于空白字符。

9. **F09 · P2：正在画二维多边形时，右键平移失效。**

   复现：添加一个 Forbidden 顶点后右拖 50 像素，固定屏幕点对应的格子始终 `(32,67)`，地图没有移动。`mouseMoveEvent()` 先把 last_mouse_position 更新为当前位置，再计算平移差，结果恒为 0。

   位置：[mouseMoveEvent()](../apps/agt_map_studio/src/occupancy/OccupancyViewer.cpp:318)。建议分开当前光标位置与拖动上一位置，或先计算 delta 再更新位置。

10. **F10 · P2：视图与菜单/工具栏的勾选状态不同步。**

    复现：自动打开二维地图后，View 菜单仍显示 3D Point Cloud checked=1、2D Navigation Map checked=0。show_2d_view/show_3d_view 修改可见性，却没有同步 QActionGroup。open_pcd() 重置 SelectionManager 的隐藏/隔离标志时，也没有回填对应 QAction。

    位置：[视图切换](../apps/agt_map_studio/src/ui/MainWindow.cpp:1383)、[创建视图 action](../apps/agt_map_studio/src/ui/MainWindow.cpp:298)。建议用一个 ViewState/InteractionState 设置入口同步页面、工具栏、action 和键盘行为。

11. **F11 · P2：三维编辑指纹漏了实际多边形坐标。**

    复现：两个操作的 ID、AABB、点数、顶点数相同，仅一个顶点从 `(0,0)` 改为 `(99,99)`；active_fingerprint 完全相同。序列化的规则内容已经不同。问题来自指纹只记录 polygon_xy.size()，还没有编码 has_z_range 和完整点索引；此项证明指纹属性不完整，尚未证明当前正常 UI 操作一定能撞上此场景。

    位置：[active_fingerprint()](../apps/agt_map_studio/src/selection/SelectionManager.cpp:180)。建议对规范化命令内容/精确索引计算指纹，和持久化表示使用同一编码，避免各自维护字段列表。

**额外代码路径风险：尚未完成端到端验证**

- **手工打开的二维图被当作当前 PCD 的 Fresh 导航层。** [open_occupancy_map()](../apps/agt_map_studio/src/ui/MainWindow.cpp:854) 没有匹配地图包或来源信息便 mark_done。独立二维查看应有独立文档状态；绑定到当前点云项目时应明确记录来源和一致性状态。当前环境缺少发布工具，未复现真实错包发布。
- **算法按钮可用性判断和真实注册目录不一致。** [algorithm_available()](../apps/agt_map_studio/src/workflow/WorkflowSession.cpp:170) 只看 profile/环境变量；[WorkflowPanel](../apps/agt_map_studio/src/ui/WorkflowPanel.cpp:253) 据此禁用第 3 步。如果某个安装算法只要求 map_pcd、没有 profile/环境变量，而且通用转换器缺失，实际能被 runner 发现的算法会被界面挡住。建议以异步查询得到的 catalog 为唯一可用性依据。
- **Clean Map 导出缺少覆盖确认和整体提交。** [固定 clean_map 路径](../apps/agt_map_studio/src/ui/MainWindow.cpp:1041) 和 [直接写文件](../apps/agt_map_studio/src/selection/SelectionManager.cpp:264) 会覆盖同一目录；规则/元数据失败时可能留下新 PCD 和旧旁路文件。会话 YAML 也以普通 ofstream 截断写入。建议推广二维导出已有的临时目录提交机制，单文件使用 QSaveFile，并显式检查 flush/close。
- **发布完成后激活读取的是当前目标。** [发布回调](../apps/agt_map_studio/src/ui/MainWindow.cpp:1963) 从当前 session 重新取 PublishTarget；运行中修改 map_id/version/activate 会使“已创建的目标”和“要激活的目标”不同。建议将点击运行时的目标按值保存在 RunContext。未调用真实发布/激活服务。
- **Quick Occupancy Preview 忽略三维删除状态。** [project()](../apps/agt_map_studio/src/ui/MainWindow.cpp:1082) 读取原始 PCL source，红色删除点仍进入投影。预览弹窗明确说明它不是正式输出，但没有清楚说明是否反映当前编辑。建议明确标为原始点云预览，或统一读取有效点云。
- **换包/恢复失败不是全程事务。** 包内 PGM 做了预检查，但 open_pcd 清空旧二维状态后，历史 sidecar 仍可能解析失败；open_session 也先替换 PCD 再恢复二维。建议先完整构造候选文档，再提交。此次没有用用户原文件制造失败。

**体验优化建议**

- 默认主任务明确为“查看/编辑地图”。二维保存、三维编辑保存、生成新结果作为主入口；发布流程可折叠。只有二维图时，完整五步发布面板的 Missing/Blocked 信息占据注意力，却不帮助完成当前任务。
- Save 2D Map、Save Session、Save Refinement History、Export Edited PGM、Apply 2D Patch、Confirm & Save 同时存在；部分 preview 导出还会更新 saved_2d_directory/editor_map_path。建议让保存只表示可恢复编辑，导出选择产物，发布另有明确目标。合并底层实现后再精简菜单，保留必要的专业选项。
- 双视图的 Undo/Delete 应跟随“最后活动的编辑文档”，并显示 Undo 2D / Undo 3D；目前点击工具栏会使 hasFocus 判断倾向另一个视图。全局 R action 只连接三维 reset，而二维控件也定义 R reset，需要桌面快捷键分发测试后统一路由。
- 当前格式只支持 PGM/trinary/零 yaw，打开时已经明确拒绝其他模式，这个保护应保留。可在文件选择器提前显示限制；是否增加 PNG/yaw 支持取决于实际导入需求，不必先扩功能。
- 状态栏应保留任务完成/保存提示和未保存标记。PointCloudViewer 的周期性 FPS 状态信号会覆盖二维/任务提示。单独使用文档状态、任务通知和可选性能信息控件更清楚。
- 补近期文件、记住上次目录/窗口布局、拖放地图包和错误中的输出路径入口。文件弹窗的非系统实现已有输入回归；此次没有证据支持把它改回系统弹窗。
- README 的 N/S/D 与实际 N/B/X 不一致，旧 Generate Occupancy Map、Save View 等名称和输出描述也过时，2026-10-06 的旧算法数值容易被误当作当前结果。帮助和菜单应由同一 action/配置定义生成，并给历史数值注明版本。

**性能优化建议**

1. **先去掉每帧重复工作。** [stats_text()](../apps/agt_map_studio/src/viewer/PointCloudViewer.cpp:204) 每次调用扫描三遍点状态；paintGL 每帧调用，FPS 刷新还额外调用。[tick()](../apps/agt_map_studio/src/viewer/PointCloudViewer.cpp:575) 每 16ms 无条件 update。建议缓存 selected/deleted/visible 数量，编辑时更新；静止或隐藏时停止渲染，用实际 elapsed time 驱动移动。此为代码复杂度结论，没有测得真实 GPU FPS 提升比例。
2. **把文件读取、投影、保存和注册查询移出 GUI 线程。** open_pcd 同步读 PCL、展开字段、算 SHA；Quick Preview 同步投影；算法列表 [run_blocking(...,10000)](../apps/agt_map_studio/src/ui/NavigationAlgorithms.cpp:32) 会等待；取消 [waitForFinished(3000)](../apps/agt_map_studio/src/tools/ExternalToolRunner.cpp:100) 也会阻塞。使用异步 QProcess/工作线程，提供明确状态，结果提交带 revision 检查。
3. **Fresh 检查集中缓存。** 一次 refresh 会多次解析 result.json、遍历资产目录和读取栅格散列。将可用性和新鲜度结果缓存为一份状态快照，在输入/文件变化时失效；明确验证按钮做较重检查。
4. **精简内存重复。** intensity 既有独立 vector，又在 scalar_fields 中复制；除测试外没有独立 intensity 的使用者。地图还分别存放在 RefinementModel 和 OccupancyViewer，更新时整图重建。先移除可证明多余的数据，再考虑共享不可变基图、仅刷新改动区域。
5. **给日志缓冲设界限。** 界面 QPlainTextEdit 已限制 4000 行，但 ExternalToolRunner 的 buffer_ 无限增长，结果又复制一次，注册算法回调还把已流式显示过的完整日志重新追加。建议磁盘保留完整日志，内存只保留错误尾部，避免重复显示。
6. **大点云显示采用单独 LOD 和空间索引。** 保留原始数据用于导出，显示点采样、命中查询使用索引。选择结果必须映射回原始 source_indices；该优化需要测量典型地图后再投入，不能靠丢弃原始字段换取速度。

**精简代码的顺序**

- `MainWindow.cpp` 为 2047 行，混合菜单构建、文件验证、保存、算法编排、发布和两类编辑。抽出 Document/SessionController、MapStorage、WorkflowExecutor；MainWindow 保留动作连接与显示。先覆盖 F01–F05 的行为契约，再移动代码，降低重构时的数据丢失风险。
- 将 open_occupancy_map/load_navigation_dir_into_2d 的加载、历史验证、逐格匹配统一成候选文档加载器；将普通保存、preview、patch、review 使用统一导出服务。各入口仍可有不同用途，但共享提交和状态更新规则。
- 将 erase/draw/fill/forbidden 共用的 id/type/timestamp、execute、undo/redo 和状态通知抽成小助手；不同栅格语义继续清楚表达。避免为了减少行数引入难追踪的宏。
- CMake 把核心和 GUI 分成可复用 library/object target；现有测试多次编译相同 GridMap/RefinementModel/commands，GUI smoke 也重新编译整个程序。二维/文件对话框测试目前受默认 OFF 的 STUDIO_ALGORITHM_SMOKE 控制，建议普通 BUILD_TESTING 就包含不依赖数据集的 UI 回归，真实数据集测试另设开关。
- 清理已确认无使用的 `rebuild_overrides_from_history()` 声明、OccupancyGridWriter include；SelectionBox 的空 cpp 可按项目编译组织决定是否保留。优先级低于状态和存储一致性。
- 用命令 enum/类型化序列化替代多处字符串判断，并让指纹从同一序列化表示生成。减少“新增编辑类型后只改了一半入口”的风险。

**验证证据与边界**

- 当前构建成功：`cmake --build .studio-build/agt_map_studio -j2`。
- 当前 CTest：11 个入口，10 通过，1 因 AGT_MAP_PROFILE 未设置而跳过；7 个 gtest 入口的 XML 共 29 个用例、0 failure/error。通过并不代表数据集集成也通过。
- 执行器 unittest：9 个用例通过，覆盖注册、参数、输入变化、失败和取消。
- 额外诊断使用真实 Studio 已编译代码和临时数据，包含一项假外部工具的异步状态复现。详细输出见 [repro_results.txt](../artifacts/agt_map_studio_audit_20261008/repro_results.txt)，源码见 [repro.cpp](../artifacts/agt_map_studio_audit_20261008/repro.cpp)，重建脚本见 [build_repro.py](../artifacts/agt_map_studio_audit_20261008/build_repro.py)。诊断程序退出 0 表示复现程序执行完毕，不能解释为这些错误已修复。
- 两个疑点未被认定为错误：本机 Qt5 top camera 的矩阵可逆且目标变换正确；当前 1199 列、0.1m、负原点的角点往返检查没有偏格。没有把代码外观上的怀疑写成已确认问题。
- 完整窗口的离屏展示尝试遇到 GLX context 创建失败，随后诊断改为不显示完整 GL 窗口。真实桌面 OpenGL 渲染、高 DPI 和快捷键分发未验证；这次 GLX 错误不足以认定用户桌面的 Studio 存在同样故障。
- 没有重跑整套荔枝园算法，没有调用真实发布/激活，没有修改地图。现有 PGM/零 yaw 格式限制和禁行区需要导航端显式接入的事实继续成立。

建议实施批次：先处理 F01–F05 和精确输出回归，再处理 F06–F11 与导入/导出保护，然后做渲染、异步 I/O 和界面精简。收益最大的改进是让“看见的编辑、保存的编辑、执行的编辑”有同一个可靠来源。
