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
    ParallelGateway,
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
    None,
    All,
    Any,
};

struct DistributionSpec
{
    DistributionType type{DistributionType::Static};
    double first{0.0};
    double second{0.0};
};

struct GeneratorSpec
{
    DistributionSpec interval_distribution{};
    std::size_t entity_count{0};
    std::string entity_type;
};

struct TaskSpec
{
    DistributionSpec duration_distribution{};
    ResourceStrategy resource_strategy{ResourceStrategy::None};
};

struct NodeDefinition
{
    std::string id;
    std::string name;
    NodeType type{NodeType::Task};
    std::optional<GeneratorSpec> generator;
    std::optional<TaskSpec> task;
};

struct SequenceFlowDefinition
{
    std::string id;
    std::string source_id;
    std::string target_id;
};

struct ResourceDefinition
{
    std::string id;
    std::string name;
    int capacity{0};
};

struct SimulationModel
{
    std::string process_id;
    std::string process_name;
    std::unordered_map<std::string, NodeDefinition> nodes;
    std::unordered_map<std::string, ResourceDefinition> resources;
    std::vector<SequenceFlowDefinition> flows;
    std::unordered_map<std::string, std::vector<std::string>> outgoing;
    std::unordered_map<std::string, std::vector<std::string>> incoming;
    std::unordered_map<std::string, std::vector<std::string>> task_resources;
    std::vector<std::string> start_node_ids;
};

} // namespace flux