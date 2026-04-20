# 引擎说明

## 实现说明

### 设计原则

引擎本质上是一个离散事件模拟器。核心事件按 `(time, order)` 排序进入优先队列，等待资源的请求则通过统一的 pending queue 和候选最小堆做仲裁，在同一时间戳末尾按 "oldest feasible first at the same timestamp" 语义统一分配。

### 关键做法

- 队列 key 分成两类：只需要一种资源的请求按资源建队列，需要多种资源的请求按任务建队列。这样既统一了控制流，又保留了释放时的定点唤醒性能。
- 资源释放时不会扫描全部等待请求，而是只重挂两个方向的队首：当前资源自己的资源队列队首，以及通过“资源 -> 相关任务队列”反向索引找到的受影响任务队列队首。这就是为什么释放时不用扫描全世界。
- 候选仲裁统一到一个最小堆里。堆键是请求顺序 `order`，并且只有当候选真正浮到堆顶时，才检查 token 是否已失效、队首是否已变化、当前资源是否真的可分配。
- 同一时间戳的等待分配统一在批次末尾触发，这保证了 "oldest feasible first at the same timestamp" 语义。

这个实现里统一的是一套框架和流程，没有统一成“全都按一个维度分组”，原因很直接：

- 需要一种资源的请求，按资源建队列最自然
- 需要多种资源的请求，按任务建队列最自然

### 心智模型

真正的等待请求会放在等待队列里；每条等待队列只把自己的队首代表送到候选堆；候选堆再按顺序挑出最优先且可行的那个请求。

所以这套调度系统是两层结构：

- 第一层：很多条等待队列
- 第二层：每条队列的队首代表组成一个候选堆

## 引擎结构

当前实现按四个角色组织：

- `RunState`：负责事件队列、调度顺序，以及把资源、token、等待仲裁三块状态串成一条主流程。
- `ResourceManager`：负责资源运行态、分配与释放、队列长度、占用时间和汇总统计。
- `TokenManager`：负责 token 生命周期、held resources、combine 历史快照，以及 split 时的恢复和派生。
- `PendingManager`：负责等待队列、候选堆、受影响队列重挂，以及同一时间戳末尾的统一仲裁。

## 主流程

主流程可以按下面这条线来理解：

1. `Engine::run` 先创建 `RunState`，初始化资源运行态，并调度所有开始事件。
2. 事件按 `(time, order)` 从优先队列里取出；同一时间戳的事件会作为一个批次一起处理。
3. `GenerateEntity` 负责创建 token，并把它送到开始事件的下游节点。
4. `ArriveNode` 负责判断 token 到达的是任务、结束事件还是网关；如果是任务，再决定直接启动、进入等待，还是走 combine / release-resource 这些特殊路径。
5. `FinishTask` 负责释放资源、更新 held resources 或 combine 状态、记录完成事件，再把 token 调度到下游。
6. 每个时间批次的原始事件处理完以后，才统一执行一次 `resolve_pending`，这就是 "oldest feasible first at the same timestamp" 语义的保证。

## 核心名词

### `ResourceManager`

**`ResourceRuntime`**

`ResourceRuntime` 表示一个资源在仿真过程里的实时状态。

里面最关键的是这些字段：

- `capacity`：容量上限。
- `in_use`：当前正在占用多少容量。
- `busy_unit_time`：累计忙碌时间，用来算利用率。
- `max_queue_length`：这个资源参与等待时观察到的最大排队长度。
- `total_wait_time` 和 `allocation_count`：后面一起用来算平均等待时间。

可以把它理解成资源侧的一本运行账。

**`resource_entities_`**

`resource_entities_` 是资源 id 到 registry entity 的映射。

`ResourceManager` 自己不直接存整份 `ResourceRuntime`，而是通过这个映射去 registry 里取资源组件。这样资源状态仍然统一放在同一个 ECS 容器里。

**`resource_queue_lengths_`**

`resource_queue_lengths_` 记录每个资源当前关联的等待长度。

注意这里不是“资源自己的队列长度”这么简单，而是“所有涉及这个资源的等待请求总共给它带来的排队长度”。后面做 timeline 和 max queue length 统计时，都要依赖这份计数。

**`allocate_resources_if_possible`**

`allocate_resources_if_possible` 表示“按当前资源状态，判断一个任务现在能不能真正拿到资源”。

它只负责一件事：检查可分配性并返回这次应该拿到哪些资源，不负责真正落账。

**`apply_allocation` / `apply_release`**

这两个函数负责把资源状态真正写进去。

- `apply_allocation`：增加 `in_use`，累计等待时间，写 allocation timeline。
- `apply_release`：减少 `in_use`，更新 busy time，写 release timeline。

也就是说：

- `allocate_resources_if_possible` 负责“能不能拿”
- `apply_allocation` / `apply_release` 负责“拿到了以后怎么记账”

**`finalize`**

`finalize` 在仿真结束时统一产出资源汇总。

它会把 busy time、idle time、utilization 这些指标整理成最终的报告。

### `TokenManager`

**`ProcessToken`**

`ProcessToken` 表示流程实体当前对应的 token 本体。

里面记录的是实体 id、实体类型、token id 和创建时间。可以把它理解成“流程里正在流动的那张卡片”。

**`HeldResources`**

`HeldResources` 表示这个 token 当前手上还持有着哪些资源。

这份集合主要给 `acquireResource` / `releaseResource` 这类任务使用。它的重点是“资源现在仍然被这个 token 持有”。

**`CombineHistory`**

`CombineHistory` 表示 combine 之后保留下来的历史快照。

它是为了后面的 split，尤其是 `_method=restore` 时，能够把原来参与 combine 的成员重新恢复出来。

**`CombineBatch`**

`CombineBatch` 表示当前这个 batch token 是由哪些成员 token 合成出来的。

它主要服务于 combine 任务完成后的销毁逻辑：batch 结束以后，原来那批成员 token 需要一起清掉。

**`snapshot_token` / `create_restored_token`**

这一对函数负责 combine / split 之间最关键的那段数据传递：

- `snapshot_token` 把 token 及其 combine 历史拍成一个可恢复快照。
- `create_restored_token` 把这个快照重新变回一个真实 token。

可以把它理解成“保存现场”和“恢复现场”。

**`schedule_split_outputs`**

`schedule_split_outputs` 负责 split 任务生成下游 token 的具体细节。

如果是 `_method=ratio`，它会直接派生新的子 token；如果是 `_method=restore`，它会从 combine 历史里把原成员恢复出来。之后再按 `one_off` 或均匀间隔把这些输出送往下游。

### `PendingManager`

**`PendingTaskRequest`**

`PendingTaskRequest` 表示一个“正在等待资源的请求”。

里面最重要的字段有这些：

- `order`：排队号。越小越早来。
- `token`：是哪一个流程实体在等。
- `task_id`：等的是哪个任务节点。
- `arrival_time`：从什么时候开始等，后面要拿来算等待时间。

可以把它理解成一张排队小票。

**`PendingQueueScope`**

`PendingQueueScope` 表示“这条等待队列是按什么维度组织的”。

有两种：

- `Resource`：按资源建队列。
- `Task`：按任务建队列。

**`PendingQueueKey`**

`PendingQueueKey` 是等待队列的地址。

它由两部分组成：

- `scope`
- `id`

例如：

- `Resource + R1` 表示“资源 R1 的等待队列”
- `Task + Task_need_all` 表示“任务 Task_need_all 的等待队列”

`PendingQueueKey` 表示“请求该进哪条队”。

**`pending_requests_`**

`pending_requests_` 是所有等待队列的大仓库：

- key：`PendingQueueKey`
- value：这条 key 对应的一条 FIFO 队列

也就是说，系统里同时会有很多条等待队列，而不是所有请求混在一条总队列里。

**`pending_queue_key_for_task`**

`pending_queue_key_for_task(task_id)` 的作用是：给一个任务，决定它该进哪种队列。

规则：

- 只需要一种资源，进资源队列
- 需要多种资源，进任务队列

本质上就是一个“分桶器”。

**`PendingCandidate`**

`PendingCandidate` 表示某条队列当前派出来参加竞争的“队首代表”。

里面有两样关键东西：

- `order`: 代表这条队列里最老的那个请求的排队号
- `key`: 代表这条队列的地址，也就是 `PendingQueueKey`

系统不会让每条队列里的所有请求都进候选堆，只会让队首进。这样堆更小，也更容易维护。

**`pending_candidates_`**

`pending_candidates_` 是候选最小堆。

可以把它看成“总叫号屏”。

- 每条等待队列只把自己的第一名挂上去
- 总叫号屏按 `order` 从小到大处理

所以：

- `pending_requests_` 存的是全部排队请求
- `pending_candidates_` 存的是每条队列的当前代表

**`task_queue_ids_by_resource_`**

`task_queue_ids_by_resource_` 是一个反向索引。

意思是：给一个资源 id，查出哪些任务队列依赖这个资源。

例如：

- `R1 -> [Task_A, Task_B]`

表示只要 `R1` 状态变了，`Task_A` 和 `Task_B` 这两条任务队列就值得重新评估。

**`rearm_resource_queues`**

`rearm` 是“重新挂回待检查状态”的意思。

`rearm_resource_queues(resource_id)` 的作用是：某个资源状态变化后，把受影响的队列重新挂回候选堆。

它会做两件事：

- 把这个资源自己的资源队列队首重新挂回去
- 通过 `task_queue_ids_by_resource_` 找到依赖这个资源的任务队列，也把它们的队首重新挂回去

这就是“定点唤醒”的意思：只通知受影响的队列，不通知无关队列。

**`next_pending_candidate`**

`next_pending_candidate()` 表示“从候选堆里找出下一个真正可以启动的请求”。

它做的事可以拆成几步：

1. 看候选堆堆顶。
2. 清理这条队列前面已经失效的请求。
3. 检查堆顶代表是不是还对应当前队首。
4. 尝试分配资源。
5. 如果现在真的能分到资源，就返回这个候选。
6. 如果还不行，就继续看下一个。

这里的关键点是：更老的请求先看，但如果更老的请求此刻依然不可行，也不会把后面可行的请求永远堵死。

**`take_front_request`**

`take_front_request` 表示把队首请求真正取出来。

流程很标准：

- 从这条队列弹出队首
- 从候选堆弹出对应代表
- 如果队列还没空，再把新的队首挂回去

这很像窗口叫到号以后，第一位出列，第二位补到最前面。

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
| `t=4` | 引擎开始挑候选                | 候选里至少有 `Task_A(order=10)` 和 `Task_B(order=11)` | 先看更老的 `Task_A`  |
| `t=4` | 检查 `Task_A` 能否分配        | `R1` 和 `R2` 都空闲                                   | `allocate` 成功      |
| `t=4` | `Task_A` 启动                 | 从等待队列出队，资源正式占用                          | 这一步是 `start`     |
| `t=4` | 再看 `Task_B`                 | `R1` 已被 `Task_A` 占用                               | 这次分配失败，继续等 |

把这个过程展开成四个动作，就是下面这样。

### 第一步：rearm

`R1` 和 `R2` 释放时，会触发 `rearm_resource_queues`。

这一步不会扫描所有等待请求，只会把受影响的队列重新挂回候选堆：

- `R1` 自己的资源队列
- 依赖 `R1` 的任务队列
- `R2` 自己的资源队列
- 依赖 `R2` 的任务队列

在这个例子里，最重要的两条受影响队列是：

- `resource_queue_key(R1)`，里面队首是 `Task_B`
- `task_queue_key(Task_A)`，里面队首是 `Task_A`

### 第二步：candidate

这些队首被挂到候选堆以后，候选堆会按 `order` 从小到大处理。

所以即使 `Task_B` 只需要一个资源，看起来更“容易”，也不会直接插队，因为候选堆先看到的是：

- `Task_A(order=10)`
- `Task_B(order=11)`

### 第三步：allocate

`next_pending_candidate()` 取到 `Task_A` 后，会尝试分配资源。

这一步检查的是：

- `R1` 是否可用
- `R2` 是否可用

此刻两个资源刚好都空了，所以分配成功。

### 第四步：start

分配成功以后，`Task_A` 会被真正启动：

- 从等待队列里弹出
- 从候选堆里弹出对应代表
- 记录资源占用
- 写入 `task_start`

这一步完成以后，`Task_A` 就不再是“等待请求”，而是“正在运行的任务”。

### 补充说明

还可以再看一个只改一处条件的变体。

如果 `t = 4` 时只有 `R1` 释放，而 `R2` 还没释放，会发生什么？

- `Task_A` 还是更老
- 但 `Task_A` 此刻需要 `R1 + R2`，仍然不可行
- `Task_B` 只需要 `R1`，已经可行

这时结果就会变成：

- `Task_A` 先被检查
- 发现目前分不到完整资源
- 不会启动
- 接着检查 `Task_B`
- `Task_B` 启动

这就是 `oldest feasible first` 的重点：

- older first：更老的先看
- feasible first：但必须“现在真的能启动”
