# flux

Flux 是一个面向供应链业务流程的高性能仿真内核，采用 Data-Oriented Design 与 ECS 架构。

Flux 寓意为供应链的流动，同时暗示数据在内存中的流动与事件流处理。

## 架构概览

```
BPMN File -> Parser -> Model (ECS Graph) -> Engine (DOD + EnTT) -> Reporter -> Output
```

## 支持范围

支持的元素：

- `startEvent`
- `task`
- `endEvent`
- `exclusiveGateway`
- `dataStoreReference`
- `sequenceFlow`
- `association`/`dataInputAssociation`/`dataOutputAssociation`

支持的能力：

- 固定随机种子，结果可复现
- 资源策略：`All`、`Any`
- 显式资源生命周期：`acquireResource`、`releaseResource`
- 合并活动：按比例 `ratio` 进行合并
- 拆分活动：按比例 `ratio` 进行拆分，或者按最近一次合并记录进行 `restore`
- 网关语义：`exclusiveGateway`，支持按权重随机路由，或按属性精确分流
- 输出报表，包括实体事件日志、资源占用时间线、资源利用率等信息
- 时间是无量纲时间，单位可由用户定义

## 构建运行

### 环境要求

1. C++ 编译器，需要支持 C++20
2. XMake 3.0+
3. Python 3.9+（仅打包安装 Python SDK 时需要）

### 构建

第一次构建时会自动下载依赖库，请确保网络畅通。

构建所有可执行文件：

```bash
xmake build
```

### 运行

`xmake run flux <file> [--seed <seed>]`

- `file`: 输入文件，位置参数，必填
- `--seed`: 随机种子，可省略，默认是 `42`

以下两条命令等价：

```bash
xmake run flux data/demo.bpmn
xmake run flux data/demo.bpmn --seed 42
```

### Python SDK

可以作为 Python 包安装，供用户在 Python 环境中调用。

打包 Python SDK：

```bash
python -m build python
```

打包后会在 `python/dist/` 目录生成 `.whl` 和 `.tar.gz` 文件。
若在同一台机器上打包和安装，推荐 `.whl`；如果需要跨机器安装，推荐 `.tar.gz`：

```bash
pip install python/dist/xxx.whl
pip install python/dist/xxx.tar.gz
```

包名是 `flux`，支持 Python `3.9+`，提供 `flux.run(file, seed=42)`

它的行为和 CLI 一致：读取 BPMN，执行仿真，并把 CSV 写到 `output/`。

可以直接运行根目录脚本：

```bash
python run.py data/demo.bpmn
python run.py data/demo.bpmn --seed 42
```

### 调试

仓库内提供 VS Code 调试配置： `.vscode/tasks.json` 和 `.vscode/launch.json`。

- `debug flux`：启动主程序调试，启动时会提示输入 BPMN 文件路径，默认值是 `data/demo.bpmn`。
- `debug test`：启动测试程序调试。

## 输出文件

程序会固定在 `output/` 目录生成文件，比如输入 `data/demo.bpmn`，会生成：

- `events_demo.csv`：实体事件日志
- `resource_timeline_demo.csv`：资源占用时间线
- `resource_summary_demo.csv`：资源利用率和等待统计

输出文件名规则是：`<报表名>_<输入文件名>.csv`。

## 扩展属性

仿真参数都放在 BPMN 的 `extensionElements` 里，当前按 `camunda:properties` 读取。

### 开始事件

必填属性：

- `_initiatorType`
- `_entityType`

支持类型：`random`、`external`

当 `_initiatorType=random` 时，还必须提供 `_entityCount` 表示生成实体的数量，并提供 `_distributionType`，其取值是分布类型：`static`、`uniform`、`exponential`、`normal`、`lognormal`。

当 `_initiatorType=external` 时：

- 按开始事件 id 读取文件 `data/external/<startEvent id>.csv`
- CSV 必须有表头，且第一列表头必须是 `time`
- 生成时刻取自 `time` 列，解析阶段会按 `time` 升序稳定排序后再调度生成

### 分布属性

支持分布：

- `static`: 固定间隔，属性是 `_staticInterval`
- `uniform`: 均匀分布，属性是 `_min` 和 `_max`
- `exponential`: 指数分布，属性是 `_mean`
- `normal`: 正态分布，属性是 `_mean` 和 `_standardDeviation`
- `lognormal`: 对数正态分布，属性是 `_mean` 和 `_standardDeviation`

校验规则：

- 解析阶段会校验参数，拒绝明显落到负时间域的输入
- `normal` 为了性能会把负值截到 `0`，不会重采样

### 任务

必填属性：

- `_taskType`

支持类型：`delay`、`transport`、`acquireResource`、`releaseResource`、`combine`、`split`

规则：

- 资源可以通过 `association` 绑定，也可以通过 `dataInputAssociation` / `dataOutputAssociation` 关联到 `dataStoreReference`
- 需要资源的任务在绑定一种资源时可省略 `_resourceStrategy`；绑定多种资源时必须提供 `_resourceStrategy=all|any`

#### delay

必填：`_distributionType`

说明：普通耗时任务，可绑定资源。

#### transport

必填：`_distributionType`、`_distance`

说明：普通耗时任务，可绑定资源。`_distance` 会累计到运输距离结果。

#### acquireResource

必填：至少绑定 1 种资源

说明：瞬时完成。任务会先申请并持有资源，直到后续显式释放。实体到达 `endEvent` 时如果仍持有资源，不会自动释放，而是保持占用直到仿真结束。持有资源的实体不支持进入 `combine` / `split` 任务。

#### releaseResource

必填：无

说明：瞬时完成，不支持 `_resourceStrategy`。如果任务绑定了资源，只释放“当前持有资源”和“绑定资源”的交集；如果没有绑定资源，则释放当前持有的全部资源。

#### combine

必填：`_method`、`_distributionType`

说明：按累计阈值合并。处理到第 `n` 个输入实体时，累计产出数为 `floor(n / _ratio)`；只有当这个累计值增长时才会启动新的合并输出。不足 1 个完整输出的尾差会继续等待，直到后续输入跨过阈值；如果流程结束仍未跨过阈值，则尾差直接丢弃。该任务也可以绑定资源。

支持方法：

- `_method=ratio`：还需要 `_ratio` 和新的 `_entityType`，表示 `N -> 1` 合并。`_ratio` 支持大于等于 `1` 的整数或浮点数，例如 `38` 个输入经过 `_ratio=3.8` 的合并后会产生 `10` 个新实体
- `_method=quantity`：目前只是占位，解析阶段会直接报不支持

#### split

必填：`_method`、`_oneOff=true|false`、`_distributionType`

说明：拆分任务，可绑定资源。`_oneOff=true` 表示全部子实体在拆分结束后一次性下发；`_oneOff=false` 表示按 `总耗时 / 子实体数` 的间隔逐个下发。

支持方法：

- `_method=ratio`：还需要 `_ratio` 和新的 `_entityType`，表示 `1 -> M` 拆分。处理到第 `n` 个输入实体时，累计产出数为 `floor(n * _ratio)`，每次只补齐新增的输出，因此 `_ratio` 支持正整数和正浮点数
- `_method=restore`：要求输入实体之前由 `combine` 生成；会按最近一次未还原的合并记录恢复原始实体ID、类型和数量。支持嵌套。
- `_method=property`：还需要 `_propertyName`，表示按实体属性里的正整数数量拆分。该属性来自 `_initiatorType=external` 的 CSV 自定义列名。拆分后子实体类型保持不变，并继承原实体属性；如果属性缺失、不是正整数，或值小于等于 `0`，会直接报错

### 网关

必填：`_criteria`

支持类型：`weight`、`property`

说明：用于根据配置把实体分流到不同出边。

- 当 `_criteria=weight` 时：按出边权重比例随机分配实体。
  - 要求：每条出边的 `sequenceFlow.name` 为正数权重（例如 `1`、`2`、`3.5`）。
  - 解析阶段会校验 `_criteria` 是否存在以及每条出边是否都提供了正数权重。

- 当 `_criteria=property` 时：按 `_propertyName` 的属性值做精确匹配分支。
  - 要求：必须提供 `_propertyName`；每条出边的 `sequenceFlow.name` 必须为非空且在同一网关下唯一。
  - 运行时如果实体缺少该属性或属性值无法匹配任何出边，会直接报错终止。

规则举例：

- 权重示例：三条出边权重为 `1`、`2`、`3`，长期比例接近 `1/6`、`2/6`、`3/6`。
- 属性示例：外部 CSV 含 `action` 列，网关配置 `_criteria=property` 和 `_propertyName=action`，若两条出边 `sequenceFlow.name` 为 `get`、`put`，则 `action=get` 的实体走 `get` 分支，`action=put` 的实体走 `put` 分支。

### 资源

必填属性：

- `_resourceType`
- `_capacity`

## 最小示例

仓库里的 [demo.bpmn](data/demo.bpmn) 是一个可直接运行的完整例子。该例子表达的是：每隔 `10` 个时间单位生成 1 个 `customer`，一共生成 `3` 个；每个实体进入一个任务，任务耗时服从 `uniform(10, 20)`；任务需要 1 个柜员资源。

## 代码结构

```text
data/
  demo.bpmn        示例 BPMN
  tests/           测试 BPMN
  golden/          golden CSV
python/
  README.md        Python SDK 说明
  flux/            Python SDK 入口
  setup.py         Python SDK 打包脚本
src/
  main.cpp          CLI 入口
  python_module.cpp Python 绑定入口
  core/
    app.hpp/.cpp       顶层调度
    model.hpp          数据模型
    parser.hpp/.cpp    输入解析
    engine.hpp/.cpp    仿真引擎
    reporter.hpp/.cpp  报表输出
    tools.hpp/.cpp     常用工具
tests/
  *_tests.cpp      测试代码
  test_support.hpp 辅助工具
```

## 实现说明

完整的实现说明和引擎结构见 [docs/engine-note.md](docs/engine-note.md)。
