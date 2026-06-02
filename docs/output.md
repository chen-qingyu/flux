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

事件日志覆盖所有节点类型（开始事件、任务、网关、结束事件），因此使用 `node_id` / `node_name` 而非 `task_*`。`task_summary` 仅包含任务节点，故使用 `task_*`。

## task_summary

任务级活动统计，每个任务一行。

### 排序规则

按 `(task_name, task_id)` 升序稳定排序。

### 列定义

| 列名                   | 类型    | 说明                                 |
| ---------------------- | ------- | ------------------------------------ |
| `task_name`            | string  | 任务名称，取自 `<task name="...">`   |
| `task_id`              | string  | 任务 ID，取自 `<task id="...">`      |
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

## task_timeline

任务状态时间线，记录每个任务在任意时刻的等待、运行、完成计数。每发生一次状态变化记一行。

### 列定义

| 列名        | 类型    | 说明               |
| ----------- | ------- | ------------------ |
| `time`      | double  | 事件发生时间       |
| `task_id`   | string  | 任务 ID            |
| `task_name` | string  | 任务名称           |
| `waiting`   | integer | 当前排队的实体数   |
| `running`   | integer | 当前执行中的实体数 |
| `completed` | integer | 累计完成的实体数   |

### 状态变化点

| 触发事件                         | `waiting` | `running` | `completed` |
| -------------------------------- | :-------: | :-------: | :---------: |
| 实体到达后资源不足，进入等待队列 |    +1     |     —     |      —      |
| 实体开始执行（从等待队列来）     |    -1     |    +1     |      —      |
| 实体不等待直接开始执行           |     —     |    +1     |      —      |
| 实体完成执行                     |     —     |    -1     |     +1      |

## resource_timeline

资源状态时间线，记录每次资源分配、释放与排队事件。

### 列定义

| 列名            | 类型    | 说明                                                     |
| --------------- | ------- | -------------------------------------------------------- |
| `time`          | double  | 事件发生时间                                             |
| `resource_id`   | string  | 资源 ID                                                  |
| `resource_name` | string  | 资源名称                                                 |
| `change_type`   | string  | 变化类型：`acquire` 分配、`release` 释放、`enqueue` 排队 |
| `in_use`        | integer | 事件发生后该资源的占用数                                 |
| `available`     | integer | 事件发生后该资源的可用数（容量 - in_use）                |
| `queue_length`  | integer | 事件发生时该资源的等待队列长度                           |
| `task_id`       | string  | 触发事件的任务 ID                                        |
| `task_name`     | string  | 触发事件的任务名称                                       |

## resource_summary

资源利用率和等待统计汇总。

### 排序规则

按 `(resource_name, resource_id)` 升序稳定排序。

### 列定义

| 列名                | 类型    | 说明                       |
| ------------------- | ------- | -------------------------- |
| `resource_name`     | string  | 资源名称                   |
| `resource_id`       | string  | 资源 ID                    |
| `capacity`          | integer | 资源容量                   |
| `busy_time`         | double  | 资源忙碌时长（时间段并集） |
| `idle_time`         | double  | 资源空闲时长               |
| `utilization`       | double  | 资源利用率                 |
| `max_queue_length`  | integer | 最大等待队列长度           |
| `average_wait_time` | double  | 平均等待时间               |
| `allocation_count`  | integer | 资源被分配的总次数         |
