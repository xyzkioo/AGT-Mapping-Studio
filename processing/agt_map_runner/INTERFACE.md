# 通用算法接入接口

对应 `agt_map_runner` 当前实现，更新日期：2026-10-06。

## 调用边界

```text
Studio / 工具箱 → map_runner → 注册描述中的算法命令 → 结果文件
                       ↑
              地图数据包 + 算法描述文件
```

地图包提供数据路径；算法包提供执行程序、默认参数和输出声明；执行器负责发现、检查、运行和记录结果。新增算法不需要修改 Studio 内部架构。当前接口为独立进程 CLI，不是 ROS topic、service 或 action。

## 调用入口

```bash
# 列出算法及其对当前地图缺少的输入
ros2 run agt_map_runner map_runner --list-json --map-package /maps/input

# 运行指定算法；输出目录必须尚不存在
ros2 run agt_map_runner map_runner --map-package /maps/input \
  --algorithm agt.offline_navigation --output /maps/job \
  --param radius=0.25 --param neighbors=6

# 工具箱入口，参数相同
bash scripts/process_map.sh --map-package /maps/input \
  --algorithm agt.offline_navigation --output /maps/job
```

`--package` 是 `--map-package` 的别名。可用 `--profile FILE` 指定数据配置、`--point-cloud FILE` 覆盖输入点云。未指定算法时，仅在注册表中恰好有一个算法时自动选择。

`--check-only` 检查注册信息、所需路径、参数及输出路径绑定，不调用算法、不创建任务目录，也不验证点云内容或算法程序能否正常运行。

## 地图数据输入

| 绑定名称 | 来源 |
|---|---|
| `map_package` | 地图目录 |
| `map_pcd` | `map.pcd`，或 `--point-cloud` |
| `poses` / `poses_timed` | `poses.txt` / `poses_timed.txt` |
| `patches` / `metadata` | `patches` / `metadata.yaml` |
| `profile` | 显式配置，或目录内的 `processing_profile.json` |
| 其他资源名称 | 配置的 `assets` 字段，路径相对配置文件目录解析 |

算法只声明自己需要的输入，不必要求所有地图文件。地图配置不决定算法默认参数；无需额外的 `request.json`，执行器不做输入 SHA256 校验。输入路径存在不代表内容格式已通过验证，算法仍需检查自己的数据契约。

## 算法注册

安装描述到 `share/<算法包>/algorithms/*.yaml`，加载该包的 ROS 工作区 overlay 后自动发现。开发时也可通过 `AGT_ALGORITHM_PATH` 添加描述目录，多个目录以冒号分隔。算法 ID 必须唯一。

以下示例假设 `my_filter` 可执行程序接受对应参数，并生成 `map.pcd`：

```yaml
schema_version: 1
id: example.my_filter
name: My point cloud filter
required_inputs: [map_pcd]
command:
  - ros2
  - run
  - my_algorithm_package
  - my_filter
  - --input
  - '{map_pcd}'
  - --output
  - '{artifact_dir}'
  - --radius
  - '{radius}'
parameters:
  radius: {type: float, default: 0.2, min: 0.001, max: 5}
outputs:
  - {type: point_cloud, path: '{artifact_dir}/map.pcd'}
```

```cmake
install(FILES algorithms/my_filter.yaml
  DESTINATION share/${PROJECT_NAME}/algorithms)
```

算法包还须安装自己的可执行程序。命令是参数数组，直接启动，不进行 shell 展开；需要环境设置或特殊启动逻辑时由算法包提供包装程序。占位符可引用输入名称、参数名称、`output`（任务目录）和 `artifact_dir`（任务目录下的 `artifacts`）。执行器不预先创建 `artifacts`，算法负责创建它。

参数类型支持 `int`、`float`、`string`，默认值来自描述文件，`--param NAME=VALUE` 只覆盖本次调用；未知参数及超出声明范围的数值会报错。参数名不能覆盖输入名称或执行器保留绑定。

Studio 可读取的输出类型为 `map_package`、`point_cloud`、`occupancy_map`。地图包仍须符合 Studio 原有地图包格式；仅声明类型不会使任意目录自动成为有效地图包。二维输出路径指向 `map.yaml`，其引用的图片也须有效。

## 取消注册与恢复

执行器只扫描描述目录中的 `*.yaml` 文件。直接删除对应的注册 YAML 即可取消注册：算法程序、源码和地图数据保留，但该算法不再显示在 Studio 的可用算法列表中。每个描述文件代表一个算法；同一包有多个描述时，只删除选中的那一个。

对已经编译安装的算法，应修改安装目录中的描述文件，不能只修改源码副本。下面以工具仓库 `.studio-install` 内的局部投影复现模块为例：

```bash
cd $REPO_ROOT

# 取消注册，仅删除这一份安装后的描述文件
rm .studio-install/agt_local_ground_projection/share/agt_local_ground_projection/algorithms/local_ground.yaml
```

恢复注册时，将算法包源码中的对应 YAML 复制回原安装位置，或重新编译安装算法包。

执行器每次调用都会重新扫描注册文件。Studio 下次点击运行时重新获取算法列表，已经启动的处理任务不受此次删除影响。若启动环境中的 `AGT_MAP_ALGORITHM` 仍指定已取消的 ID，需要取消该环境变量或改为可用 ID，否则会提示算法未注册，不会自动换用其他算法。

通过 `AGT_ALGORITHM_PATH` 提供的描述也可以直接删除；从该变量移除一个目录则会同时隐藏该目录下的全部算法。还应确保其他已加载目录中没有同 ID 的注册文件。

取消注册不等于卸载算法程序。重新编译安装算法包可能重新生成 YAML，使算法再次注册；届时需再次删除注册文件。当前没有独立的取消注册 CLI 或 Studio 管理按钮。

验证当前注册列表：

```bash
ros2 run agt_map_runner map_runner --list-json
```

## 结果与错误

```text
job/
  result.json
  algorithm.log
  artifacts/       # 算法生成的文件
```

`result.json` 由执行器写入，算法不必自行生成。完成示例：

```json
{
  "schema_version": 1,
  "algorithm_id": "example.my_filter",
  "status": "complete",
  "parameters": {"radius": 0.2},
  "inputs": {"map_package": "/maps/input", "map_pcd": "/maps/input/map.pcd"},
  "outputs": [{"type": "point_cloud", "path": "/maps/job/artifacts/map.pcd"}],
  "error": null
}
```

状态为 `running`、`complete`、`failed` 或 `cancelled`。只有算法退出码为零且所有声明输出存在，才标记完成；声明输出必须位于任务目录内。调用方应同时检查进程成功退出、完成状态和所需输出。日志不是稳定的数值进度协议。

正常完成返回 0，运行错误返回 1，参数解析错误返回 2，取消返回 130。任务开始前的错误可能没有 `result.json`；失败和取消可能留下部分文件，不自动删除，重试应使用新目录。完成后，执行器也将结果记录复制到声明的 `map_package` 输出目录。

## Studio 对接

新结果额外记录 `command` 和 `provenance`（注册描述原文、输入文件大小/修改时间及目录文件清单）。运行期间输入或注册发生变化则报错；Studio 用这些记录判断结果过期，并记录实际算法 ID 和参数到发布追溯信息。不对输入计算 SHA256；同时保持文件大小和修改时间的内容变化不能被该检查检测，历史结果缺少 provenance 时保留旧检查行为。

当前注册算法尚未支持人工三维删除规则。Studio 检测到这种编辑会明确报错，不再悄悄改用通用转换器；独立二维编辑和保存不受影响。

Studio 调用执行器发现算法，按所需输入过滤可用项。只有一个兼容算法时自动选择；有多个时显示选择列表，也可用 `AGT_MAP_ALGORITHM` 指定 ID。Studio 当前使用描述中的默认参数，未提供通用参数编辑表单；CLI 可通过 `--param` 覆盖。

`AGT_MAP_PROFILE` 可指定数据配置，`AGT_MAP_OUTPUT_ROOT` 可指定工作目录。任务位于工作目录的 `processing_runs/run_<时间>/`；结果读取 `result.json` 的类型和路径，不再依赖某个算法的固定输出目录。

现有组合算法的数据要求、直接调用入口及过滤规则见 [agt_map_processing 接口说明](../agt_map_processing/INTERFACE.md)。
