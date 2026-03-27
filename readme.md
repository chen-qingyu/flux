# flux

`flux` 是一个面向 BPMN 子集的离散事件仿真器。输入是 BPMN，输出是 CSV。

当前实现重点是把流程、资源和时间语义跑通，而不是做完整 BPMN 引擎。

## 支持范围

支持的节点和关系：

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
- 输出三份报表：`events.csv`、`resource_timeline.csv`、`resource_summary.csv`

限制：

- 输入格式目前只有 BPMN
- 任务目前只支持 `delay`
- 时间是无量纲时间，不绑定秒或分钟

## 构建和运行

构建：

```bash
xmake build
```

运行：

```bash
xmake run flux --input data/demo.bpmn
```

完整写法：

```bash
xmake run flux --input data/demo.bpmn --seed 42
```

- `--input`: 输入文件，必填
- `--seed`: 随机种子，可省略，默认是 `42`

## 输出文件

程序会固定在 `output/` 目录生成文件，文件名会附带输入文件名的 stem。比如输入 `data/demo.bpmn`，会生成：

- `events_demo.csv`：实体事件日志
- `resource_timeline_demo.csv`：资源占用时间线
- `resource_summary_demo.csv`：资源利用率和等待统计

## 扩展属性怎么写

仿真参数都放在 BPMN 的 `extensionElements` 里，当前按 `camunda:properties` 读取。

### 开始事件

开始事件至少要提供：

- `_initiatorType`
- `_entityCount`
- `_entityType`

### 任务

任务至少要提供：

- `_taskType=delay`
- `_distributionType`

如果任务需要资源，还可以提供：

- `_resourceStrategy=all|any`

然后再通过 `association` 把任务连到 `dataStoreReference`。

### 资源

资源至少要提供：

- `_resourceType=resource`
- `_capacity`

### 分布属性

当前支持这些分布：

- `Static`: 固定间隔，属性是 `_staticInterval`
- `Uniform`: 均匀分布，属性是 `_min` 和 `_max`
- `Exponential`: 指数分布，属性是 `_mean`
- `Normal`: 正态分布，属性是 `_mean` 和 `_standardDeviation`
- `LogNormal`: 对数正态分布，属性是 `_mean` 和 `_standardDeviation`

## 最小 BPMN 示例

仓库里的 [data/demo.bpmn](data/demo.bpmn) 是一个可直接运行的完整例子，表达的是：每隔 `10` 个时间单位生成 1 个 `customer`，一共生成 `3` 个；每个实体进入一个任务，任务耗时服从 `uniform(10, 20)`；任务需要 1 个柜员资源。

## 代码结构

```text
data/
  demo.bpmn        示例 BPMN
  tests/           测试 BPMN
  golden/          golden CSV
src/
  main.cpp         CLI 入口
  model.hpp        数据模型
  parser.hpp/.cpp  BPMN 解析
  engine.hpp/.cpp  仿真运行时
  reporter.hpp/.cpp CSV 输出
  tools.hpp/.cpp   常用模型访问工具
tests/
  *_tests.cpp   测试代码
```
