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
- `iscsim:transportTask`
- `iscsim:acquireResourceTask`
- `iscsim:releaseResourceTask`
- `iscsim:combineTask`
- `iscsim:splitTask`
- `endEvent`
- `exclusiveGateway`
- `parallelGateway`
- `dataStoreReference`
- `sequenceFlow`
- `association`/`dataInputAssociation`/`dataOutputAssociation`

支持的能力：

- 固定随机种子，结果可复现
- 资源策略：`All`、`Any`
- 显式资源生命周期：`acquireResource`、`releaseResource`
- 合并活动：按比例 `ratio` 进行合并
- 拆分活动：按比例 `ratio` 进行拆分，或者按最近一次合并记录进行 `restore`
- 网关语义：`XOR`、`AND`
- 输出报表，包括实体事件日志、资源占用时间线、资源利用率等信息
- 时间是无量纲时间，单位可由用户定义

## 构建和运行

构建所有二进制目标：

```bash
xmake build
```

运行：

```bash
xmake run flux data/demo.bpmn
xmake run flux data/demo.bpmn --seed 42
```

- `file`: 输入文件，位置参数，必填
- `--seed`: 随机种子，可省略，默认是 `42`

打包 Python SDK：

```bash
python -m build python
```

安装 Python SDK：

```bash
pip install python/dist/flux-*.whl
```

包名是 `flux`，支持 Python `3.9+`，提供 `flux.run(file, seed=42)`

它的行为和 CLI 一致：读取 BPMN，执行仿真，并把 CSV 写到 `output/`。

可以直接运行根目录脚本：

```bash
python run.py data/demo.bpmn
python run.py data/demo.bpmn --seed 42
```

## 输出文件

程序会固定在 `output/` 目录生成文件，比如输入 `data/demo.bpmn`，会生成：

- `events_demo.csv`：实体事件日志
- `resource_timeline_demo.csv`：资源占用时间线
- `resource_summary_demo.csv`：资源利用率和等待统计

输出文件名规则是：`<报表名>_<输入文件名>.csv`。

## 扩展属性

仿真参数都放在 BPMN 的 `extensionElements` 里，当前按 `camunda:properties` 读取。

### 开始事件

开始事件至少要提供：

- `_initiatorType`
- `_entityCount`
- `_entityType`

当前开始事件只支持 `_initiatorType=random`。

当 `_initiatorType=random` 时，会继续读取字段 `_distributionType`，其取值是分布类型：`static`、`uniform`、`exponential`、`normal`、`lognormal`。

### 任务

任务至少要提供 `_taskType`，当前支持 `delay`、`transport`、`acquireResource`、`releaseResource`、`combine` 和 `split`。

当 `_taskType=delay|transport` 时，任务需要耗时，因此必须提供 `_distributionType`。

当 `_taskType=transport` 时，还必须提供 `_distance`，表示该次运输任务完成后累计到引擎结果中的运输距离。

当 `_taskType=acquireResource` 时，任务会瞬时完成，但会先申请并持有绑定资源，直到后续显式释放。该任务至少要绑定一种资源；如果绑定了多种资源，则必须提供 `_resourceStrategy=all|any`。

当 `_taskType=releaseResource` 时，任务也会瞬时完成，并且不支持 `_resourceStrategy`。如果该任务绑定了资源，则只释放当前实体持有且与绑定列表相交的资源；如果没有绑定资源，则释放当前实体持有的全部资源。

当 `_taskType=combine` 时，任务必须使用 `iscsim:combineTask` 元素，并且当前只支持 `_method=ratio`。还需要提供：

- `_ratio`：按 `N -> 1` 合并
- `_entityType`：合并后的新实体类型
- `_distributionType` 及对应分布参数：表示整批合并耗时

实体到达合并活动后，只有凑满比例的一批才会真正启动任务；不足比例的余数会继续停留在该活动中等待。合并活动可以像普通耗时任务一样绑定资源并使用 `_resourceStrategy`。

当 `_taskType=split` 时，任务必须使用 `iscsim:splitTask` 元素，并提供 `_oneOff=true|false` 与 `_distributionType`。当前支持两种方法：

- `_method=ratio`：还需要 `_ratio` 与新的 `_entityType`，表示 `1 -> M` 拆分
- `_method=restore`：要求输入实体之前由 combine 生成；会按最近一次未还原的合并记录恢复原始实体 id、类型和数量

`_oneOff=true` 表示所有子实体在拆分耗时结束后统一下发；`_oneOff=false` 表示按照 `总耗时 / 子实体数` 的间隔逐个下发。拆分活动同样可以绑定资源并使用 `_resourceStrategy`。

`quantity` 相关方法当前只占位，解析阶段会直接报不支持。

普通 `delay` / `transport` 任务如果只关联一种资源，可以省略 `_resourceStrategy`；如果任务关联了多种资源，则必须提供 `_resourceStrategy=all|any`。

资源绑定可以通过 `association`，也可以通过 `dataInputAssociation` / `dataOutputAssociation` 把任务连到 `dataStoreReference`。

如果实体到达 `endEvent` 时仍持有资源，则这些资源不会自动释放，而是保持占用直到仿真结束。

当前限制：正在持有资源的实体不能进入 `combine` / `split` 任务；如果需要这一语义，需要后续单独定义资源在批处理拆并过程中的归属规则。

### 资源

资源至少要提供：

- `_resourceType`
- `_capacity`

### 分布属性

当前支持这些分布：

- `static`: 固定间隔，属性是 `_staticInterval`
- `uniform`: 均匀分布，属性是 `_min` 和 `_max`
- `exponential`: 指数分布，属性是 `_mean`
- `normal`: 正态分布，属性是 `_mean` 和 `_standardDeviation`
- `lognormal`: 对数正态分布，属性是 `_mean` 和 `_standardDeviation`

分布属性的参数在解析阶段会进行校验，时间语义要求采样结果非负，因此解析层会拒绝明显落到负时间域的输入。

另外，正态分布当前为了性能采取负值截到 `0` 的处理，而不是重采样。

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

引擎本质上是一个离散事件模拟器。核心事件按 `(time, order)` 排序进入优先队列，等待资源的请求则通过统一的 pending queue 和候选最小堆做仲裁，在同一时间戳末尾按 "oldest feasible first at the same timestamp" 语义统一分配。

完整的实现说明、机制解释，以及手推时间线示例，见 [docs/engine-note.md](docs/engine-note.md)。
