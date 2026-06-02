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
- 合并活动：支持按比例 `ratio` 合并，或按属性分组后再按比例 `groupRatio` 合并，并可按数量属性计算等效实体数
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
