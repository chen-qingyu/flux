# 引擎说明

## 实现说明

### 设计原则

引擎本质上是一个离散事件模拟器。核心事件按 `(time, order)` 排序进入优先队列，等待资源的请求则通过统一的 pending queue 和候选最小堆做仲裁，在同一时间戳末尾按 "oldest feasible first at the same timestamp" 语义统一分配。

### 关键做法

- 单资源请求按资源排队，多资源请求按任务排队。这样既保留了统一仲裁流程，也避免释放资源时扫描全部等待者。
- 每条等待队列只把自己的队首送进候选堆；候选堆按 `order` 处理，真正浮到堆顶时才检查 token 是否有效、队首是否仍然匹配、资源是否真的可分配。
- 同一时间戳的原始事件先处理完，最后再统一执行 `resolve_pending`，所以同一时刻的资源仲裁结果稳定且可解释。

### 心智模型

可以把这套调度系统想成两层：

- 第一层是很多条等待队列，真正的请求都在这里排队。
- 第二层是一个候选堆，每条队列只把自己的队首代表挂上来。

这样就能做到一句话概括：更早的请求先看，但只有当前真的可行的请求才会启动。

## 引擎结构

当前实现按四个角色组织：

- `RunState`：负责事件队列、调度顺序、任务启动与完成、split 输出调度、gateway 路由，以及把资源、token、等待仲裁三块状态串成一条主流程。
- `ResourceManager`：负责资源运行态、分配与释放、队列长度、占用时间和汇总统计。
- `TokenManager`：负责 token 组件访问、held resources、combine 历史快照，以及 combine 等待成员队列。
- `PendingManager`：负责等待队列、候选堆、受影响队列重挂，以及同一时间戳末尾的统一仲裁。

## 主流程

主流程可以按下面这条线来理解：

1. `Engine::run` 先创建 `RunState`，初始化资源运行态，并调度所有开始事件。
2. 事件按 `(time, order)` 从优先队列里取出；同一时间戳的事件会作为一个批次一起处理。
3. `GenerateEntity` 负责创建 token，并把它送到开始事件的下游节点；如果开始事件是 `external`，则会把 CSV 当前行里除 `time` 外的列一起挂到 token 属性上。
4. `ArriveNode` 负责判断 token 到达的是任务、结束事件还是网关；任务会决定直接启动还是进入等待，也会处理 combine 和 release-resource 这类特殊路径；网关则按 `_criteria` 做权重路由或属性路由。
5. `FinishTask` 负责释放资源、更新 held resources 或 combine 状态、记录完成事件；combine 会清掉成员 token，split 会销毁原 token，其他路径再把 token 调度到下游。
6. 每个时间批次的原始事件处理完以后，才统一执行一次 `resolve_pending`，这就是 "oldest feasible first at the same timestamp" 语义的保证。

## 核心名词

### `RunState`

`RunState` 是运行期总控。它自己持有事件优先队列、随机采样器、结果对象，以及 `ResourceManager`、`TokenManager`、`PendingManager` 三块子状态。

可以把它理解成一条主循环外加几个关键函数：

- `start_or_enqueue_task`：决定任务是直接启动，还是进入等待队列。
- `start_task` / `handle_finish_task`：负责任务开始和结束时的状态落账。
- `schedule_split_outputs`：负责 split 任务的输出派生与下游调度。
- `select_exclusive_gateway_target`：负责 exclusive gateway 的属性路由和权重路由。

### `ResourceManager`

`ResourceManager` 管资源运行态和资源报表，可以直接理解成“资源侧账本”。

核心组件：

- `allocate_resources_if_possible` 只判断“现在能不能拿到资源”。
- `apply_allocation` / `apply_release` 真正更新占用、等待时间和 timeline。
- `finalize` 在仿真结束时产出资源汇总。

### `TokenManager`

`TokenManager` 管 token 相关的运行期附加状态。

核心组件：

- `ProcessToken` 和 `HeldResources`：表示 token 本体，以及它当前持有的资源。
- `CombineHistory`：给 `_method=restore` 留下可恢复的历史。
- `combine_waiting_`：保存 combine 任务正在等待凑齐的成员。

它本身不负责驱动 split 或 combine 的主流程，只负责提供这些状态给 `RunState` 使用。

### `PendingManager`

`PendingManager` 管等待资源的请求，可以理解成“排队系统”。

核心组件：

- `pending_requests_` 里存所有等待队列。
- `pending_candidates_` 里只存每条等待队列的队首代表。
- `rearm_resource_queues` 在资源变化后，只重挂受影响的队列。
- `next_pending_candidate` 从候选堆里找下一个当前可启动的请求。

它保证的是“oldest feasible first”：更早的请求先看，但如果更早的请求此刻不可行，也不会把后面可行的请求堵死。

## 手推时间线

下面用一个具体时间线，把仲裁动作连起来看一次。

场景如下：

- 资源 `R1` 和 `R2`，容量都是 1
- 等待任务 `Task_A` 需要 `R1 + R2`
- 等待任务 `Task_B` 只需要 `R1`
- `Task_A` 比 `Task_B` 更早到达
- 在 `t = 4` 时，`R1` 和 `R2` 同时释放

时间线：

| 时间  | 发生的事                      | 队列状态                                              | 备注                 |
| ----- | ----------------------------- | ----------------------------------------------------- | -------------------- |
| `t=1` | 旧任务占用 `R1`、`R2`         | 暂无等待                                              | 两个资源都忙         |
| `t=2` | `Task_A` 到达，需要 `R1 + R2` | `Task_A` 进入任务队列                                 | `order = 10`         |
| `t=3` | `Task_B` 到达，只需要 `R1`    | `R1` 资源队列里有 `Task_B`                            | `order = 11`         |
| `t=4` | 旧任务结束，释放 `R1`、`R2`   | 两条受影响的队列被重新挂回候选堆                      | 这一步就是 `rearm`   |
| `t=4` | 引擎开始挑候选                | 候选里至少有 `Task_A(order=10)` 和 `Task_B(order=11)` | 先看更早的 `Task_A`  |
| `t=4` | 检查 `Task_A` 能否分配        | `R1` 和 `R2` 都空闲                                   | `allocate` 成功      |
| `t=4` | `Task_A` 启动                 | 从等待队列出队，资源正式占用                          | 这一步是 `start`     |
| `t=4` | 再看 `Task_B`                 | `R1` 已被 `Task_A` 占用                               | 这次分配失败，继续等 |

这个例子能说明四步：

### 第一步：rearm

`R1` 和 `R2` 释放时，会触发 `rearm_resource_queues`。

这一步不会扫描所有等待请求，只会把受影响的队列重新挂回候选堆。

在这个例子里，最重要的两条受影响队列是：

- `resource_queue_key(R1)`，里面队首是 `Task_B`
- `task_queue_key(Task_A)`，里面队首是 `Task_A`

### 第二步：candidate

这些队首被挂到候选堆以后，候选堆会先看 `Task_A(order=10)`，再看 `Task_B(order=11)`。

### 第三步：allocate

`next_pending_candidate()` 取到 `Task_A` 后，会尝试分配资源。

此刻两个资源刚好都空了，所以分配成功。

### 第四步：start

分配成功以后，`Task_A` 会被真正启动：

- 从等待队列里弹出
- 从候选堆里弹出对应代表
- 记录资源占用
- 写入 `task_start`

这一步完成以后，`Task_A` 就不再是“等待请求”，而是“正在运行的任务”。

### 补充说明

如果 `t = 4` 时只有 `R1` 释放，而 `R2` 还没释放，会发生什么？

- `Task_A` 还是更早
- 但 `Task_A` 此刻需要 `R1 + R2`，仍然不可行
- `Task_B` 只需要 `R1`，已经可行

这时结果就会变成：

- `Task_A` 先被检查
- 发现目前分不到完整资源
- 不会启动
- 接着检查 `Task_B`
- `Task_B` 启动

这就是 `oldest feasible first` 的重点：

- older first：更早的先看
- feasible first：但必须“现在真的能启动”
