# API 文档

## 概述

flux-api 是一个 RESTful HTTP 服务，提供 BPMN 仿真运行的多实例管理。

- 所有请求/响应均为 `application/json`，CSV 下载为 `text/csv`
- 仿真在独立子进程中运行，取消直接杀进程
- 无持久化存储，服务重启后运行记录丢失

## 端点

### `POST /api/instances`

创建并启动一个仿真实例。

**请求体：**

```json
{
  "model_name": "string (必填) 模型名称，用于 CSV 文件命名",
  "model_content": "string (必填) BPMN XML 内容",
  "external_files": {
    "startEventId": "csv内容"
  },
  "random_seed": 42
}
```

| 字段             | 类型    | 必填 | 默认值 | 说明                                                       |
| ---------------- | ------- | ---- | ------ | ---------------------------------------------------------- |
| `model_name`     | string  | 是   | —      | CSV 文件名前缀，如 `demo` -> `demo-entity_events-{ts}.csv` |
| `model_content`  | string  | 是   | —      | 完整的 BPMN 2.0 XML                                        |
| `external_files` | object  | 否   | `null` | key 为 `startEvent` 的 id，value 为 CSV 内容               |
| `random_seed`    | integer | 否   | `42`   | 随机种子，相同种子 + 相同输入 = 相同输出                   |

**响应 `201`：**

```json
{
  "instance_id": "a1b2c3d4e5f6",
  "model_name": "供应链模型",
  "status": "running",
  "created_at": "2026-06-27T18:30:00+00:00"
}
```

**错误：**

| 状态码 | 说明           |
| ------ | -------------- |
| `400`  | 参数校验失败   |
| `422`  | 请求体格式错误 |

---

### `GET /api/instances`

列出所有仿真实例，按创建时间倒序。

**响应 `200`：**

```json
{
  "instances": [
    {
      "instance_id": "a1b2c3d4e5f6",
      "model_name": "供应链模型",
      "status": "completed",
      "created_at": "2026-06-27T18:30:00+00:00"
    }
  ]
}
```

---

### `GET /api/instances/{instance_id}`

获取单个实例的详细状态。

**响应 `200`：**

```json
{
  "instance_id": "a1b2c3d4e5f6",
  "model_name": "供应链模型",
  "status": "completed",
  "reports": [
    "demo-entity_events-1782558691.csv",
    "demo-resource_summary-1782558691.csv",
    "demo-resource_timeline-1782558691.csv",
    "demo-task_summary-1782558691.csv",
    "demo-task_timeline-1782558691.csv"
  ],
  "error": null,
  "created_at": "2026-06-27T18:30:00+00:00"
}
```

**状态说明：**

| status      | 说明                                     |
| ----------- | ---------------------------------------- |
| `running`   | 仿真仍在执行，`reports` 为空             |
| `completed` | 仿真完成，`reports` 包含 5 个 CSV 文件名 |
| `failed`    | 仿真失败，`error` 包含错误信息           |
| `cancelled` | 已被用户取消（已删除文件）               |

**错误：**

| 状态码 | 说明            |
| ------ | --------------- |
| `404`  | instance 不存在 |

---

### `GET /api/instances/{instance_id}/reports`

下载本次仿真全部 CSV 报表的 ZIP 压缩包。

**响应 `200`：** `Content-Type: application/zip`，文件名为 `{model_name}-reports.zip`。

ZIP 内包含 5 个 CSV 文件：

- `{model_name}-entity_events-{ts}.csv`
- `{model_name}-resource_timeline-{ts}.csv`
- `{model_name}-resource_summary-{ts}.csv`
- `{model_name}-task_summary-{ts}.csv`
- `{model_name}-task_timeline-{ts}.csv`

**错误：**

| 状态码 | 说明                        |
| ------ | --------------------------- |
| `404`  | instance 不存在或报表未生成 |

---

### `DELETE /api/instances/{instance_id}`

取消运行中的仿真并删除所有相关文件。已完成的实例同样可以删除。

**响应 `200`：**

```json
{
  "instance_id": "a1b2c3d4e5f6",
  "status": "cancelled"
}
```

**错误：**

| 状态码 | 说明            |
| ------ | --------------- |
| `404`  | instance 不存在 |

---

## 仿真输出

每次仿真产生 5 个 CSV 文件，命名规则 `{model_name}-{report}-{timestamp}.csv`：

| 文件                | 说明                                         | 详细定义               |
| ------------------- | -------------------------------------------- | ---------------------- |
| `entity_events`     | 实体事件日志，按 `(time, order)` 排序        | [output.md](output.md) |
| `task_summary`      | 任务级统计汇总，每个任务一行                 | [output.md](output.md) |
| `task_timeline`     | 任务状态时间线，每次状态变化一行             | [output.md](output.md) |
| `resource_summary`  | 资源级统计汇总，每个资源一行                 | [output.md](output.md) |
| `resource_timeline` | 资源状态时间线，包含 acquire/release/enqueue | [output.md](output.md) |

## 前端集成示例

```javascript
// 1. 上传模型并启动仿真
const { instance_id } = await fetch("/api/instances", {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({
    model_name: "供应链模型-v2",
    model_content: bpmnXml,
    random_seed: 42,
  }),
}).then((r) => r.json());

// 2. 轮询直到完成
let status = "running";
while (status === "running") {
  await new Promise((r) => setTimeout(r, 1000));
  ({ status, reports } = await fetch(`/api/instances/${instance_id}`).then(
    (r) => r.json(),
  ));
}

// 3. 下载全部报表 (ZIP)
const zip = await fetch(`/api/instances/${instance_id}/reports`).then((r) =>
  r.blob(),
);
// 前端解压 ZIP 或直接下载给用户

// 4. (可选) 清理
await fetch(`/api/instances/${instance_id}`, { method: "DELETE" });
```

## 启动服务

```bash
pip install fastapi uvicorn
python -m uvicorn server.main:app --host 0.0.0.0 --port 8000
```
