# API 文档

## 概述

flux-api 是一个 RESTful HTTP 服务，提供 BPMN 仿真运行的多实例管理。

- **Instance**：命名的模型身份，可多次运行（每次运行 BPMN/参数可不同）
- **Run**：一次仿真执行，产生 5 个 CSV 报表

所有请求/响应为 `application/json`，报表下载为 `application/zip`。

## 端点总览

| 方法     | 路径                                     | 说明                |
| -------- | ---------------------------------------- | ------------------- |
| `POST`   | `/api/instances`                         | 创建实例            |
| `GET`    | `/api/instances`                         | 实例列表            |
| `GET`    | `/api/instances/{id}`                    | 实例详情（含 runs） |
| `PATCH`  | `/api/instances/{id}`                    | 重命名实例          |
| `DELETE` | `/api/instances/{id}`                    | 删实例 + 全部 runs  |
| `POST`   | `/api/instances/{id}/runs`               | 创建运行            |
| `GET`    | `/api/instances/{id}/runs`               | 运行列表            |
| `GET`    | `/api/instances/{id}/runs/{rid}`         | 运行状态            |
| `GET`    | `/api/instances/{id}/runs/{rid}/reports` | 下载 ZIP            |
| `POST`   | `/api/instances/{id}/runs/{rid}/cancel`  | 终止运行            |
| `DELETE` | `/api/instances/{id}/runs/{rid}`         | 删除运行            |

## Instance 端点

### `POST /api/instances`

**请求：**

```json
{ "instance_name": "供应链模型" }
```

| 字段            | 类型           | 必填 | 说明                       |
| --------------- | -------------- | ---- | -------------------------- |
| `instance_name` | string (1-128) | 是   | 可重命名，路径字符自动消毒 |

**响应 `201`：**

```json
{
  "instance_id": "a1b2c3d4e5f6",
  "instance_name": "供应链模型",
  "created_at": "2026-06-27T18:30:00+00:00",
  "run_count": 0
}
```

### `GET /api/instances`

```json
{
  "instances": [
    {
      "instance_id": "a1b2c3d4e5f6",
      "instance_name": "供应链模型",
      "created_at": "...",
      "run_count": 3
    }
  ]
}
```

### `GET /api/instances/{instance_id}`

返回实例详情 + 所有 runs：

```json
{
  "instance_id": "a1b2c3d4e5f6",
  "instance_name": "供应链模型",
  "created_at": "...",
  "run_count": 2,
  "runs": [
    {
      "run_id": "r1r2r3r4r5r6",
  "instance_id": "a1b2c3d4e5f6",
  "instance_name": "供应链模型",
  "status": "completed",
      "random_seed": 42,
      "reports": ["供应链模型-entity_events-{ts}.csv", ...],
  "error": null,
      "created_at": "..."
}
  ]
}
```

### `PATCH /api/instances/{instance_id}`

```json
{ "instance_name": "新名称" }
```

### `DELETE /api/instances/{instance_id}`

级联删除该实例下所有 runs 及文件。

## Run 端点

### `POST /api/instances/{instance_id}/runs`

**请求：**

```json
{
  "model_content": "<bpmn:definitions ...>",
  "external_files": { "startEventId": "csv内容" },
  "random_seed": 42
}
```

| 字段             | 类型    | 必填 | 默认值 | 说明                             |
| ---------------- | ------- | ---- | ------ | -------------------------------- |
| `model_content`  | string  | 是   | —      | BPMN XML 内容                    |
| `external_files` | object  | 否   | `null` | key=startEventId, value=CSV 内容 |
| `random_seed`    | integer | 否   | `42`   | 随机种子                         |

**响应 `201`：**

```json
{
  "run_id": "r1r2r3r4r5r6",
  "instance_id": "a1b2c3d4e5f6",
  "instance_name": "供应链模型",
  "status": "running",
  "random_seed": 42,
  "reports": [],
  "error": null,
  "created_at": "..."
}
```

### `GET /api/instances/{instance_id}/runs`

返回该实例下所有 runs，按时间倒序。

### `GET /api/instances/{instance_id}/runs/{run_id}`

返回单个 run 状态：

| status      | 说明                                 |
| ----------- | ------------------------------------ |
| `running`   | 仿真执行中，`reports` 为空           |
| `completed` | 完成，`reports` 包含 5 个 CSV 文件名 |
| `failed`    | 失败，`error` 包含错误信息           |
| `cancelled` | 已终止                               |

### `GET /api/instances/{instance_id}/runs/{run_id}/reports`

下载 ZIP `{instance_name}-reports-{ts}.zip`，含 5 个 CSV：

- `{instance_name}-entity_events-{ts}.csv`
- `{instance_name}-task_summary-{ts}.csv`
- `{instance_name}-task_timeline-{ts}.csv`
- `{instance_name}-resource_summary-{ts}.csv`
- `{instance_name}-resource_timeline-{ts}.csv`

仅 `completed` 状态的 run 可下载，否则返回 `409`。

### `POST /api/instances/{instance_id}/runs/{run_id}/cancel`

终止运行。杀进程、删文件，保留记录。仅 `running` 状态的 run 可取消，否则返回 `409`。

### `DELETE /api/instances/{instance_id}/runs/{run_id}`

删除运行。杀进程、删文件、移除记录。

## 前端集成

```javascript
// 1. 创建实例
const { instance_id } = await fetch("/api/instances", {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ instance_name: "供应链模型" }),
}).then((r) => r.json());

// 2. 创建运行
const { run_id } = await fetch(`/api/instances/${instance_id}/runs`, {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ model_content: bpmnXml, random_seed: 42 }),
}).then((r) => r.json());

// 3. 轮询
let status = "running";
while (status === "running") {
  await new Promise((r) => setTimeout(r, 1000));
  ({ status } = await fetch(
    `/api/instances/${instance_id}/runs/${run_id}`,
  ).then((r) => r.json()));
}

// 4. 下载 ZIP
const zip = await fetch(
  `/api/instances/${instance_id}/runs/${run_id}/reports`,
).then((r) => r.blob());
```

## 启动

```bash
python -m uvicorn server.main:app --host 127.0.0.1 --port 8000
```
