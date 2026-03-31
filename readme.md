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
- `parallelGateway`
- `dataStoreReference`
- `sequenceFlow`
- `association`

支持的能力：

- 固定随机种子，结果可复现
- 资源策略：`All`、`Any`
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

### 任务

任务至少要提供 `_taskType`。

如果任务需要耗时，必须提供 `_distributionType`。

如果任务需要资源，必须提供 `_resourceStrategy=all|any`，然后再通过 `association` 把任务连到 `dataStoreReference`。

### 资源

资源至少要提供：

- `_resourceType`
- `_capacity`

### 分布属性

当前支持这些分布：

- `Static`: 固定间隔，属性是 `_staticInterval`
- `Uniform`: 均匀分布，属性是 `_min` 和 `_max`
- `Exponential`: 指数分布，属性是 `_mean`
- `Normal`: 正态分布，属性是 `_mean` 和 `_standardDeviation`
- `LogNormal`: 对数正态分布，属性是 `_mean` 和 `_standardDeviation`

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

引擎本质上是一个离散事件模拟器。核心事件按 `(time, order)` 排序进入优先队列，`time` 决定仿真时间推进，`order` 保证同一时间戳内执行顺序稳定且可复现。开始事件负责生成实体，任务事件负责申请和释放资源，网关事件负责分支与汇合。

资源等待采用 "oldest feasible first at the same timestamp" 语义。直观地说，系统会先处理完同一时间戳内的所有原始事件，再统一判断等待中的请求里谁现在真的能启动：更老但暂时还不满足资源条件的请求，不会挡住后面已经可行的请求；而如果多个请求在这一时刻都可行，则仍按更早的请求顺序启动。这样既保持了结果稳定，也让 FIFO 行为更容易理解和验证。

一些加速方法：

- 单资源请求按资源分桶。每个资源维护自己的等待队列，资源一旦有空位，只需要看该队列的队首，再和其他资源的队首一起按顺序仲裁。这避免了高并发单资源场景下反复扫描整条等待集合
- 多资源请求保留在统一的 pending 队列中。因为这类请求是否可行取决于多个资源的组合状态，所以仍需要在每个时间戳末尾做一次前向扫描，找出当前真正可行的候选
- 同一时间戳的等待分配统一在批次末尾触发，而不是把“唤醒等待者”拆成额外事件重新塞回队列。这减少了无效调度，也保证了 FIFO 语义一致
