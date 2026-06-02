# 输出报表

输出文件命名规则：`<报表名>_<输入文件名>.csv`，固定在 `output/` 目录生成。

## entity_events

实体事件日志，记录每个实体在仿真生命周期中的关键事件。

### 排序规则

按事件发生顺序输出（即仿真内部事件队列的 `(time, order)` 顺序）。

### 列定义

| 列名          | 类型   | 说明                                          |
| ------------- | ------ | --------------------------------------------- |
| `time`        | double | 事件发生时间                                  |
| `entity_id`   | string | 实体 ID（全局递增整数）                       |
| `entity_name` | string | 实体名称，格式 `<节点名称>-<实体类型>-<序号>` |
| `node_id`     | string | 节点 ID                                       |
| `node_name`   | string | 节点名称                                      |
| `event_type`  | string | 事件类型                                      |

### 事件类型

| 事件类型                     | 说明                 |
| ---------------------------- | -------------------- |
| `entity_generated`           | 实体由开始事件生成   |
| `task_arrive`                | 实体到达任务节点     |
| `task_waiting_for_resources` | 实体因资源不足而等待 |
| `task_start`                 | 实体开始执行任务     |
| `task_finish`                | 实体完成执行任务     |
| `gateway_route`              | 实体经过网关分流     |
| `entity_exit`                | 实体到达结束事件     |

### 说明

事件日志覆盖所有节点类型（开始事件、任务、网关、结束事件），因此使用 `node_id` / `node_name` 而非 `activity_*`。`activity_summary` 仅包含任务节点，故使用 `activity_*`。

## activity_summary

任务级活动统计，每个任务一行。

### 排序规则

按 `(activity_name, activity_id)` 升序稳定排序。

### 列定义

| 列名                   | 类型    | 说明                                 |
| ---------------------- | ------- | ------------------------------------ |
| `activity_name`        | string  | 活动名称，取自 `<task name="...">`   |
| `activity_id`          | string  | 活动 ID，取自 `<task id="...">`      |
| `arrival_count`        | integer | 到达该活动的实体总数                 |
| `start_count`          | integer | 启动执行的实体数（<= arrival_count） |
| `busy_time`            | double  | 活动忙碌时长（时间段并集）           |
| `busy_rate`            | double  | 忙碌率，取值范围 `[0, 1]`            |
| `queue_total_time`     | double  | 所有已启动实体的等待时间总和         |
| `queue_average_time`   | double  | 所有已启动实体的平均等待时间         |
| `queue_max_time`       | double  | 所有已启动实体的最大等待时间         |
| `queue_min_time`       | double  | 所有已启动实体的最小等待时间         |
| `process_total_time`   | double  | 所有已完成实体的处理时间总和         |
| `process_average_time` | double  | 所有已完成实体的平均处理时间         |
| `process_max_time`     | double  | 所有已完成实体的最大处理时间         |
| `process_min_time`     | double  | 所有已完成实体的最小处理时间         |

### 说明

- `arrival_count - start_count` 为仿真结束时仍在等待资源而未启动的实体数；对于 combine 活动，`arrival_count` 为到达等效实体数，`start_count` 为合并后产生的实体数。
