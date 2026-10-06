# 地图处理接口说明

适用版本：`agt_map_processing 0.1.0`，2026-10-06。

## 1. 调用边界

工具箱和 Studio 通过通用 `agt_map_runner` 执行器启动注册算法，注册及统一结果格式见 [通用算法接入接口](../agt_map_runner/INTERFACE.md)。本文描述组合算法自身的输入要求和直接调用接口。算法包不依赖 Studio、Qt 或地图专用路径；地图、检测缓存与运动证据由外部数据配置提供。

```text
工具箱 / Studio → map_runner → process_map → 新的地图数据包
                                  ↑
                       原地图 + processing_profile.json
```

当前接口是 CLI，不是 ROS topic、service 或 action。Studio 用 `QProcess` 调用 `ros2 run`，收集日志和退出状态，成功后打开结果。

## 2. 处理入口

```bash
ros2 run agt_map_processing process_map \
  --package "/path/to/map_package" \
  --output "/path/to/new_result"
```

工具箱的等价入口：

```bash
bash scripts/process_map.sh --map-package "/path/to/map_package" \
  --algorithm agt.offline_navigation --output "/path/to/new_job"
```

路径含空格时加引号。调用前需要加载 ROS 2 和包含该包的工作区 overlay；工具箱脚本会加载既有工作区及仓库 `.studio-install`。

| 参数 | 必需性 | 含义 |
|---|---|---|
| `--package DIR` | 与 `--profile` 至少提供一个 | 地图数据目录；默认读取其中的 `processing_profile.json` |
| `--profile FILE` | 可选 | 显式指定数据配置；未传 `--package` 时使用配置中的 `package` |
| `--output DIR` | 必需，包括检查模式 | 新结果目录，实际处理时必须尚不存在 |
| `--octomap-builder FILE` | 可选 | 覆盖 OctoMap 可执行文件；默认使用同一安装目录的 `octomap_builder` |
| `--check-only` | 可选开关 | 检查输入文件存在性及 OctoMap 程序，不运行处理、不创建结果目录 |

```bash
ros2 run agt_map_processing process_map --package "/path/to/map_package" \
  --output "/path/to/not_created" --check-only
```

这项检查不是完整数据格式或导航安全验证。运行时仍可能因点云字段、资源或文件格式而失败。

当前组合固定为 CenterPoint 后处理、车体过滤、地面与二维孤立点过滤、OctoMap，没有单算法开关。算法默认参数保存在算法包；本次调用可通过 CLI 覆盖下列参数，地图配置不能覆盖它们。

| 参数 | 默认值 | 可接受范围 |
|---|---|---|
| `--radius` | `0.2` 米 | 大于 0，且不超过 5 |
| `--neighbors` | `5`，包含点自身 | 1–100 整数 |
| `--frames` | `3`，每层独立观测帧数 | 1–255 整数 |

例如：`process_map --package /maps/input --output /maps/result --radius 0.25 --neighbors 6`。其他算法默认值仍由包内实现和 `centerpoint_config.json` 管理。

## 3. 输入数据契约

```text
map_package/
  map.pcd
  poses.txt
  poses_timed.txt
  patches/*.pcd
  metadata.yaml
  manifest.yaml
  calibration.yaml          # 存在时随结果保留
  processing_profile.json
```

当前后处理需要配套的标记字段和检测/运动证据，不支持把任意裸 XYZ PCD 直接套入完整流程。二进制 PCD、逐帧位姿和检测缓存必须对应同一次建图；CenterPoint 不重新执行神经网络检测。

配置结构示例，文件名仅为说明，替换为实际路径：

```json
{
  "schema_version": 1,
  "name": "My map",
  "package": ".",
  "assets": {
    "detections": "assets/detections.json",
    "source": "assets/map_confidence.pcd",
    "baseline": "assets/baseline.pcd",
    "parked": "assets/parking_review.json",
    "evidence": "assets/motion_evidence.npz",
    "tracks": "assets/tracks.json"
  }
}
```

| 资产键 | 内容 |
|---|---|
| `detections` | 配套 CenterPoint 原始检测缓存 |
| `source` | 含原有运动/语义等证据字段的点云 |
| `baseline` | 既有基准处理点云 |
| `parked` | 已审核停车车辆保护证据 |
| `evidence` | 既有点级运动证据 NPZ |
| `tracks` | 配套轨迹资产，用于地面/栅格处理输入 |

所有相对路径以 **配置文件所在目录**为基准，不以代码目录或启动目录为基准。可以引用兄弟数据目录，移动时应保持完整数据目录间的相对关系。绝对路径兼容旧配置。

地图配置仅保留 `schema_version`、`name`、`package`、`assets` 和可选的 `coordinate_system` 数据说明。算法模式和默认参数由算法包决定，旧配置中的算法相关字段忽略，迁移时不再输出。

不要求 `sha256` 字段，也不比较输入文件哈希。旧配置中的该字段会被忽略。程序检查必需文件和关键帧目录，数据格式在运行时检查。`schema_version` 目前是版本标记，尚没有严格的 JSON Schema 验证。

## 4. 配置迁移接口

```bash
ros2 run agt_map_processing prepare_map --profile "/path/to/old_profile.json" \
  --output "/path/to/map_package/processing_profile.json"
```

两个参数均必需。目标父目录必须存在；目标文件已存在则拒绝覆盖。该命令仅重写相对路径并移除旧的校验与算法参数字段，不复制扫描、不计算检测、不生成输入校验值。迁移后用 `process_map --check-only` 检查。

## 5. 输出接口

```text
new_result/
  map.pcd                   # 过滤后的点云
  navigation/map.yaml       # 二维地图描述
  navigation/map.pgm        # 黑/灰/白栅格图片
  report.json               # 完成状态、计数、耗时及实现版本
  processing_profile.json   # 重新定位路径后的数据配置
  manifest.yaml
  metadata.yaml
  checksums.sha256
  poses.txt
  poses_timed.txt
  patches/                  # 保留原扫描作为观测来源
  centerpoint/              # 中间处理产物
  ground_navigation/
  octomap/
```

`report.json` 的核心字段：

| 字段 | 含义 |
|---|---|
| `status` | 成功为 `complete` |
| `output_points` | 最终点云点数 |
| `grid_counts` | 像素值 `0`、`205`、`254` 的数量：障碍、未知、空闲 |
| `new_free_cells` | OctoMap 增加的空闲格数量 |
| `algorithms` | 实际采用的组合算法标记 |
| `input_sha256_checked` | 为 `false`，不检查输入哈希 |
| `seconds` | 总处理耗时 |
| `profile_sha256` | 本次数据配置校验值 |
| `implementation_sha256` | Python 实现文件校验值 |
| `outputs.pcd` / `outputs.map_yaml` | 最终点云及二维 YAML 路径 |

调用方应同时检查正常退出、`status == complete` 及实际输出文件存在，不能仅凭日志里的“Completed”判断成功。输出清单的校验值覆盖列出的映射数据文件，不能视为整个结果目录的完整校验。

原扫描保留不代表过滤失效：`map.pcd` 是过滤后的结果，`patches` 是原始射线观测证据。重复处理生成包时，程序使用配置引用的原观测源，避免反复过滤已过滤点云。

## 6. 日志、错误和取消

标准输出提供英文阶段日志和子程序报告，目前不是稳定的 JSON 进度流；不要通过匹配某一句日志驱动业务。错误写入标准错误。

处理脚本正常返回 `0`，运行错误返回 `1`，参数解析错误返回 `2`；捕获 SIGINT/SIGTERM 时取消子进程并返回 `130`。外层 `ros2 run` 或进程管理器还可能产生启动失败、信号终止等状态，工具箱应按“正常退出且退出码为零”判定成功。

处理过程中失败或取消可能留下部分结果目录；当前没有原子发布或自动删除失败目录。没有成功报告的目录不能直接作为有效地图使用，重试必须选新目录。

## 7. Studio 对接

Studio 导航入口调用 `agt_map_runner map_runner`，执行器根据注册描述启动本包的 `process_map`；Studio 不再编译、安装算法源码或地图专用配置。默认从地图源目录找 `processing_profile.json`。注册描述位于本包的 `algorithms/offline_navigation.yaml`，参数覆盖使用执行器的 `--param radius=…` 等形式；直接调用本包仍使用 `--radius` 等参数。

| 环境变量 | 用途 |
|---|---|
| `AGT_MAP_PROFILE` | 可选，显式选择外部数据配置 |
| `AGT_MAP_ALGORITHM` | 可选，指定注册算法 ID，例如 `agt.offline_navigation` |
| `AGT_ALGORITHM_PATH` | 可选，添加算法描述文件目录，以冒号分隔 |
| `AGT_MAP_OUTPUT_ROOT` | 可选，指定处理工作目录；未设置时使用会话工作目录 |

Studio 在工作目录的 `processing_runs/run_<时间>/` 下创建任务。任务根目录含执行器的 `result.json` 和 `algorithm.log`；本组合算法的地图文件位于 `artifacts/map.pcd` 和 `artifacts/navigation/map.yaml`。成功后 Studio 按统一结果中的输出类型和路径打开地图，保存 `studio_session/studio_session.yaml`。运行期间源文件或二维编辑发生变化时，保留当前视图并提示结果位置。

过滤和八层净空判定规则没有因拆包而改变。该接口不表示导航安全认证，也不保证动态物体全部去除；二维人工编辑仍由 Studio 的独立保存接口处理。
