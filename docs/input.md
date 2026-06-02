# 输入格式

仿真参数都放在 BPMN 的 `extensionElements` 里，当前按 `camunda:properties` 读取。

## 开始事件

必填属性：

- `_initiatorType`
- `_entityType`

支持类型：`random`、`external`

当 `_initiatorType=random` 时，还必须提供 `_entityCount` 表示生成实体的数量，并提供 `_distributionType`，其取值是分布类型：`static`、`uniform`、`exponential`、`normal`、`lognormal`。

当 `_initiatorType=external` 时：

- 按开始事件 id 读取文件 `data/external/<startEvent id>.csv`
- CSV 必须有表头，且第一列表头必须是 `time`
- 生成时刻取自 `time` 列，解析阶段会按 `time` 升序稳定排序后再调度生成
- 除 `time` 外的其他列会作为字符串属性挂到生成实体上，可供属性网关、属性拆分、数量型合并等能力读取

## 分布属性

支持分布：

- `static`: 固定间隔，属性是 `_staticInterval`
- `uniform`: 均匀分布，属性是 `_min` 和 `_max`
- `exponential`: 指数分布，属性是 `_mean`
- `normal`: 正态分布，属性是 `_mean` 和 `_standardDeviation`
- `lognormal`: 对数正态分布，属性是 `_mean` 和 `_standardDeviation`

校验规则：

- 解析阶段会校验参数，拒绝明显落到负时间域的输入
- `normal` 为了性能会把负值截到 `0`，不会重采样

## 任务

必填属性：

- `_taskType`

支持类型：`delay`、`transport`、`acquireResource`、`releaseResource`、`combine`、`split`

规则：

- 资源可以通过 `association` 绑定，也可以通过 `dataInputAssociation` / `dataOutputAssociation` 关联到 `dataStoreReference`
- 需要资源的任务在绑定一种资源时可省略 `_resourceStrategy`；绑定多种资源时必须提供 `_resourceStrategy=all|any`

### delay

必填：`_distributionType`

说明：普通耗时任务，可绑定资源。

### transport

必填：`_distributionType`、`_distance`

说明：普通耗时任务，可绑定资源。`_distance` 会累计到运输距离结果。

### acquireResource

必填：至少绑定 1 种资源

说明：瞬时完成。任务会先申请并持有资源，直到后续显式释放。实体到达 `endEvent` 时如果仍持有资源，不会自动释放，而是保持占用直到仿真结束。持有资源的实体不支持进入 `combine` / `split` 任务。

### releaseResource

必填：无

说明：瞬时完成，不支持 `_resourceStrategy`。如果任务绑定了资源，只释放“当前持有资源”和“绑定资源”的交集；如果没有绑定资源，则释放当前持有的全部资源。

### combine

必填：`_method`、`_distributionType`、`_useQuantityProperty`、`_entityType`、`_ratio`

说明：按累计阈值合并。每次产出 1 个新实体都对应 1 次独立的 combine 执行，因此会各自消耗一份配置的处理时长，也会各自参与资源申请。`_useQuantityProperty=true|false` 表示是否把某个实体属性解释为“等效实体数量”；当 `_useQuantityProperty=true` 时，还必须提供 `_quantity`，且属性值必须是正整数。尾差会继续等待，直到后续输入跨过阈值；如果流程结束仍未跨过阈值，则尾差直接丢弃。该任务也可以绑定资源。

支持方法：

- `_method=ratio`：按全任务累计做 `N -> 1` 合并。未启用数量属性时，每个到达实体记作 `1` 个等效实体；启用后，等效实体数取自 `_quantity`。`_ratio` 支持大于等于 `1` 的整数或浮点数，例如 `38` 个等效实体经过 `_ratio=3.8` 的合并后会产生 `10` 个新实体
- `_method=groupRatio`：还需要 `_group`，系统会先按该属性值分组，再在每个分组内独立按比例合并；若启用了数量属性，则每个分组累计的是等效实体数

`restore` 与数量型合并兼容：如果某个实体通过数量属性贡献了多个等效实体，后续 `split restore` 会按“实际被消费的等效实体数”恢复。例如 `qty=12`、`_ratio=5` 时可产出 `2` 个合并实体，后续还原时会恢复成 `5 + 5` 个实体。

### split

必填：`_method`、`_oneOff=true|false`、`_distributionType`

说明：拆分任务，可绑定资源。`_oneOff=true` 表示全部子实体在拆分结束后一次性下发；`_oneOff=false` 表示按 `总耗时 / 子实体数` 的间隔逐个下发。

支持方法：

- `_method=ratio`：还需要 `_ratio` 和新的 `_entityType`，表示 `1 -> M` 拆分。处理到第 `n` 个输入实体时，累计产出数为 `floor(n * _ratio)`，每次只补齐新增的输出，因此 `_ratio` 支持正整数和正浮点数
- `_method=restore`：要求输入实体之前由 `combine` 生成；会按最近一次未还原的合并记录恢复原始实体ID、类型和数量。支持嵌套。
- `_method=quantity`：还需要 `_quantity`，表示按实体属性里的正整数数量拆分。该属性来自 `_initiatorType=external` 的 CSV 自定义列名。拆分后子实体类型保持不变，并继承原实体属性；如果属性缺失、不是正整数，或值小于等于 `0`，会直接报错

## 网关

必填：`_criteria`

支持类型：`weight`、`group`

说明：用于根据配置把实体分流到不同出边。

- 当 `_criteria=weight` 时：按出边权重比例随机分配实体。
  - 要求：每条出边的 `sequenceFlow.name` 为正数权重（例如 `1`、`2`、`3.5`）。
  - 解析阶段会校验 `_criteria` 是否存在以及每条出边是否都提供了正数权重。

- 当 `_criteria=group` 时：按 `_group` 的属性值做精确匹配分支。
  - 要求：必须提供 `_group`；每条出边的 `sequenceFlow.name` 必须为非空且在同一网关下唯一。
  - 运行时如果实体缺少该属性或属性值无法匹配任何出边，会直接报错终止。

规则举例：

- 权重示例：三条出边权重为 `1`、`2`、`3`，长期比例接近 `1/6`、`2/6`、`3/6`。
- 属性示例：外部 CSV 含 `action` 列，网关配置 `_criteria=group` 和 `_group=action`，若两条出边 `sequenceFlow.name` 为 `get`、`put`，则 `action=get` 的实体走 `get` 分支，`action=put` 的实体走 `put` 分支。

## 资源

必填属性：

- `_resourceType`: 当前仅支持 `resource`，表示普通资源
- `_capacity`: 资源容量，表示该资源可以同时被多少实体占用
