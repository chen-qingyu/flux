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

注意事项：

- 输入格式目前只有 BPMN
- 时间是无量纲时间

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

包名是 `flux`，支持 Python `3.9+`。

- `flux.run(file, seed=42)`

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

任务至少要提供：

- `_taskType=delay`
- `_distributionType`

如果任务需要资源，必须提供：

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

引擎对高拥塞场景做了两层优化：

- 资源等待队列长度采用增量维护，不再反复全量扫描 pending 请求。
- 对只依赖单个资源的任务，运行时会进入该资源自己的 FIFO 等待队列；资源释放后只定向唤醒该资源队列，而不是重新扫描所有等待请求。

这两点能显著改善单资源、高并发排队场景下的性能。
