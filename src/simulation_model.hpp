#pragma once

#include <optional>
#include <stdexcept>
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

inline std::string to_string(NodeType value)
{
    switch (value)
    {
        case NodeType::StartEvent:
            return "start_event";
        case NodeType::Task:
            return "task";
        case NodeType::EndEvent:
            return "end_event";
        case NodeType::ExclusiveGateway:
            return "exclusive_gateway";
        case NodeType::ParallelGateway:
            return "parallel_gateway";
    }

    return "unknown";
}

inline std::string to_string(DistributionType value)
{
    switch (value)
    {
        case DistributionType::Static:
            return "static";
        case DistributionType::Uniform:
            return "uniform";
        case DistributionType::Exponential:
            return "exponential";
        case DistributionType::Normal:
            return "normal";
        case DistributionType::LogNormal:
            return "lognormal";
    }

    return "unknown";
}

inline std::string to_string(ResourceStrategy value)
{
    switch (value)
    {
        case ResourceStrategy::None:
            return "none";
        case ResourceStrategy::All:
            return "all";
        case ResourceStrategy::Any:
            return "any";
    }

    return "unknown";
}

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

    [[nodiscard]] const NodeDefinition& node(const std::string& node_id) const
    {
        if (const auto found = nodes.find(node_id); found != nodes.end())
        {
            return found->second;
        }
        throw std::runtime_error("Unknown node id: " + node_id);
    }

    [[nodiscard]] const ResourceDefinition& resource(const std::string& resource_id) const
    {
        if (const auto found = resources.find(resource_id); found != resources.end())
        {
            return found->second;
        }
        throw std::runtime_error("Unknown resource id: " + resource_id);
    }

    void validate() const
    {
        if (process_id.empty())
        {
            throw std::runtime_error("BPMN process id is missing.");
        }
        if (start_node_ids.empty())
        {
            throw std::runtime_error("Simulation model must contain at least one start event.");
        }

        for (const auto& [node_id, definition] : nodes)
        {
            switch (definition.type)
            {
                case NodeType::StartEvent:
                    if (!definition.generator.has_value())
                    {
                        throw std::runtime_error("Start event '" + node_id + "' is missing generator settings.");
                    }
                    if (definition.generator->entity_count == 0)
                    {
                        throw std::runtime_error("Start event '" + node_id + "' must generate at least one entity.");
                    }
                    if (!outgoing.contains(node_id) || outgoing.at(node_id).empty())
                    {
                        throw std::runtime_error("Start event '" + node_id + "' must have outgoing sequence flow.");
                    }
                    break;
                case NodeType::Task:
                    if (!definition.task.has_value())
                    {
                        throw std::runtime_error("Task '" + node_id + "' is missing duration settings.");
                    }
                    if (!outgoing.contains(node_id) || outgoing.at(node_id).empty())
                    {
                        throw std::runtime_error("Task '" + node_id + "' must have outgoing sequence flow.");
                    }
                    break;
                case NodeType::EndEvent:
                    break;
                case NodeType::ExclusiveGateway:
                case NodeType::ParallelGateway:
                    if (!outgoing.contains(node_id) || outgoing.at(node_id).empty())
                    {
                        throw std::runtime_error("Gateway '" + node_id + "' must have outgoing sequence flow.");
                    }
                    break;
            }
        }

        for (const auto& flow : flows)
        {
            if (!nodes.contains(flow.source_id))
            {
                throw std::runtime_error("Sequence flow '" + flow.id + "' has unknown source node '" + flow.source_id + "'.");
            }
            if (!nodes.contains(flow.target_id))
            {
                throw std::runtime_error("Sequence flow '" + flow.id + "' has unknown target node '" + flow.target_id + "'.");
            }
        }

        for (const auto& [task_id, resource_ids] : task_resources)
        {
            if (!nodes.contains(task_id))
            {
                throw std::runtime_error("Task-resource binding references unknown task '" + task_id + "'.");
            }
            for (const auto& resource_id : resource_ids)
            {
                if (!resources.contains(resource_id))
                {
                    throw std::runtime_error("Task-resource binding references unknown resource '" + resource_id + "'.");
                }
            }
        }
    }
};

} // namespace flux