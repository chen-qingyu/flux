#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace flux
{

enum class NodeType
{
    StartEvent,
    Task,
    EndEvent,
    ExclusiveGateway,
};

enum class DistributionType
{
    Static,
    Uniform,
    Exponential,
    Normal,
    LogNormal,
};

enum class ResourceStrategy
{
    All,
    Any,
};

enum class TaskType
{
    Delay,
    Transport,
    AcquireResource,
    ReleaseResource,
    Combine,
    Split,
};

enum class CombineMethod
{
    Ratio,
    Quantity,
};

enum class SplitMethod
{
    Ratio,
    Restore,
    Quantity,
};

enum class GatewayCriteria
{
    ByWeight,
};

// 分布参数统一压平成两个数值位，具体含义由 type 决定。
struct DistributionSpec
{
    DistributionType type{DistributionType::Static};
    double first{0.0};
    double second{0.0};
};

// 起始事件负责生成业务实体，并决定相邻两次生成之间的间隔。
struct GeneratorSpec
{
    DistributionSpec interval_distribution{};
    std::size_t entity_count{0};
    std::string entity_type;
};

struct CombineSpec
{
    CombineMethod method{CombineMethod::Ratio};
    double ratio{0.0};
    std::string entity_type;
};

struct SplitSpec
{
    SplitMethod method{SplitMethod::Ratio};
    double ratio{0.0};
    bool one_off{true};
    std::string entity_type;
};

// 任务配置同时承载执行时间、资源策略和运输距离等可选语义。
struct TaskSpec
{
    TaskType type{TaskType::Delay};
    DistributionSpec duration_distribution{};
    std::optional<ResourceStrategy> resource_strategy;
    double distance{0.0};
    std::optional<CombineSpec> combine;
    std::optional<SplitSpec> split;
};

struct NodeDefinition
{
    std::string id;
    std::string name;
    NodeType type{NodeType::Task};
    std::optional<GeneratorSpec> generator;
    std::optional<TaskSpec> task;
    std::optional<GatewayCriteria> gateway_criteria;
};

struct SequenceFlowDefinition
{
    std::string id;
    std::string name;
    std::string source_id;
    std::string target_id;
    std::optional<double> weight;
};

struct ResourceDefinition
{
    std::string id;
    std::string name;
    int capacity{0};
};

// Model 是解析后的只读索引，运行阶段不会再回写 BPMN 结构。
struct Model
{
    std::string process_id;
    std::string process_name;
    std::unordered_map<std::string, NodeDefinition> nodes;
    std::unordered_map<std::string, ResourceDefinition> resources;
    std::vector<SequenceFlowDefinition> flows;
    std::unordered_map<std::string, std::vector<std::string>> outgoing;
    std::unordered_map<std::string, std::vector<std::string>> incoming;
    std::unordered_map<std::string, std::vector<std::string>> outgoing_flow_ids;
    std::unordered_map<std::string, std::size_t> flow_indexes;
    std::unordered_map<std::string, std::vector<std::string>> task_resources;
    std::vector<std::string> start_node_ids;
};

} // namespace flux