# flux Python SDK

Python bindings for the flux BPMN simulator.

## 安装

```bash
python -m build python
pip install python/dist/xxx.whl
```

若需跨机器安装，可使用 `.tar.gz`：

```bash
pip install python/dist/xxx.tar.gz
```

## 使用

```python
import flux

flux.run(
    model_name="demo",
    model_content=bpmn_string,
    output_dir="output",
    external_dir="data/external",
    random_seed=42,
)
```

| 参数            | 类型 | 默认值            | 说明                   |
| --------------- | ---- | ----------------- | ---------------------- |
| `model_name`    | str  | —                 | 模型名称，用于实例命名 |
| `model_content` | str  | —                 | BPMN XML 内容字符串    |
| `output_dir`    | str  | `"output"`        | CSV 输出目录           |
| `external_dir`  | str  | `"data/external"` | 外部 CSV 文件目录      |
| `random_seed`   | int  | `42`              | 随机种子               |

## 命令行脚本

```bash
python run.py data/demo.bpmn
python run.py data/demo.bpmn --seed 42 --output results
```
