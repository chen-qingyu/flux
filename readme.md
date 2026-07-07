# flux

Flux 是一个面向供应链业务流程的高性能仿真内核，采用 Data-Oriented Design 与 ECS 架构。

Flux 寓意为供应链的流动，同时暗示数据在内存中的流动与事件流处理。

## 架构概览

```
BPMN Content -> Parser -> Model (ECS Graph) -> Engine (DOD + EnTT) -> Reporter -> CSV
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
- 合并活动：支持按比例 `ratio` 合并，或按属性分组后再按比例 `groupRatio` 合并，并可按数量属性计算等效实体数
- 拆分活动：按比例 `ratio` 进行拆分，或者按最近一次合并记录进行 `restore`
- 网关语义：`exclusiveGateway`，支持按权重随机路由，或按属性精确分流
- 输出报表，包括实体事件日志、资源占用时间线、资源利用率等信息
- 时间是无量纲时间，单位可由用户定义

## 构建运行

### 环境要求

1. C++ 编译器，需要支持 [C++20](https://en.cppreference.com/cpp/20) 标准
2. [XMake](https://github.com/xmake-io/xmake) 3.0+

### 构建

第一次构建时会自动下载依赖库，请确保网络畅通。

构建所有可执行文件：

```bash
xmake build
```

### 运行

提供三种使用方式。

#### CLI

通过命令行直接运行仿真。

```bash
xmake run cli data/demo.bpmn --seed 42 --output output
```

输入文件路径必填；`--seed` 默认 `42`；`--output` 默认 `output`。

#### SDK

需要 Python 3.9+。作为 Python 包供上层代码调用。

```bash
pip install python/dist/xxx.tar.gz
```

```python
import flux
flux.run("demo", bpmn_string)
```

也提供命令行脚本：`python run.py data/demo.bpmn`

详见 [python/README.md](python/README.md)

#### API

通过 HTTP 服务远程调用仿真。

```bash
pip install fastapi uvicorn
python -m uvicorn server.main:app --host 127.0.0.1 --port 8000
```

详见 [docs/api.md](docs/api.md)

## 输入输出

### 输入

输入文件为 BPMN 2.0 XML 格式，遵循 BPMN 2.0 规范，并在此基础上做了适当的扩展以支持更多仿真功能。

输入的详细定义见 [docs/input.md](docs/input.md)。

### 输出

输出会生成 5 个 CSV 文件。

- `entity_events`：实体事件日志
- `task_summary`：任务侧统计汇总
- `task_timeline`：任务状态时间线
- `resource_summary`：资源侧统计汇总
- `resource_timeline`：资源状态时间线

输出的详细定义见 [docs/output.md](docs/output.md)。

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
    app.hpp/.cpp       统一入口
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
