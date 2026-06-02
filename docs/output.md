# 输出报表

## activity_summary

输出文件命名：`activity_summary_<输入文件名>.csv`。

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
