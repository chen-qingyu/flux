#include "bpmn_parser.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <magic_enum/magic_enum.hpp>
#include <pugixml.hpp>

namespace flux
{
namespace
{

std::string local_name(const char* raw_name)
{
    std::string name = raw_name == nullptr ? "" : raw_name;
    if (const auto separator = name.find(':'); separator != std::string::npos)
    {
        return name.substr(separator + 1);
    }
    return name;
}

std::string lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string read_required_attribute(const pugi::xml_node& node, const char* attribute_name, const char* context)
{
    const auto attribute = node.attribute(attribute_name);
    if (!attribute)
    {
        throw std::runtime_error(std::string(context) + " is missing required attribute '" + attribute_name + "'.");
    }
    return attribute.value();
}

std::unordered_map<std::string, std::string> read_properties(const pugi::xml_node& owner)
{
    std::unordered_map<std::string, std::string> properties;

    for (const auto& child : owner.children())
    {
        if (local_name(child.name()) != "extensionElements")
        {
            continue;
        }

        for (const auto& extension_child : child.children())
        {
            if (local_name(extension_child.name()) != "properties")
            {
                continue;
            }

            for (const auto& property_node : extension_child.children())
            {
                if (local_name(property_node.name()) != "property")
                {
                    continue;
                }

                const auto name = property_node.attribute("name").value();
                const auto value = property_node.attribute("value").value();
                if (!std::string(name).empty())
                {
                    properties.insert_or_assign(name, value);
                }
            }
        }
    }

    return properties;
}

double read_required_double(const std::unordered_map<std::string, std::string>& properties, const std::vector<std::string>& keys, const std::string& context)
{
    for (const auto& key : keys)
    {
        if (const auto found = properties.find(key); found != properties.end())
        {
            return std::stod(found->second);
        }
    }

    std::string message = context + " is missing required numeric property. Expected one of: ";
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        if (index > 0)
        {
            message += ", ";
        }
        message += keys[index];
    }
    throw std::runtime_error(message);
}

std::string read_required_text(const std::unordered_map<std::string, std::string>& properties, const std::string& key, const std::string& context)
{
    if (const auto found = properties.find(key); found != properties.end() && !found->second.empty())
    {
        return found->second;
    }
    throw std::runtime_error(context + " is missing required property '" + key + "'.");
}

template <typename Enum>
Enum read_required_enum(const std::unordered_map<std::string, std::string>& properties, const std::string& key, const std::string& context)
{
    const auto value = read_required_text(properties, key, context);
    if (const auto parsed = magic_enum::enum_cast<Enum>(value, magic_enum::case_insensitive); parsed.has_value())
    {
        return *parsed;
    }

    throw std::runtime_error(context + " uses unsupported property '" + key + "' value '" + value + "'.");
}

std::size_t read_required_count(const std::unordered_map<std::string, std::string>& properties, const std::string& key, const std::string& context)
{
    if (const auto found = properties.find(key); found != properties.end())
    {
        const auto parsed = std::stoll(found->second);
        if (parsed <= 0)
        {
            throw std::runtime_error(context + " property '" + key + "' must be greater than zero.");
        }
        return static_cast<std::size_t>(parsed);
    }
    throw std::runtime_error(context + " is missing required property '" + key + "'.");
}

DistributionSpec read_distribution(const std::unordered_map<std::string, std::string>& properties, const std::string& type_key, const std::string& context)
{
    const auto distribution_type = read_required_enum<DistributionType>(properties, type_key, context);
    switch (distribution_type)
    {
        case DistributionType::Static:
            return DistributionSpec{DistributionType::Static, read_required_double(properties, {"_staticInterval", "_staticValue", "_fixedDuration", "_duration"}, context), 0.0};
        case DistributionType::Uniform:
            return DistributionSpec{
                DistributionType::Uniform,
                read_required_double(properties, {"_uniformMin", "_min"}, context),
                read_required_double(properties, {"_uniformMax", "_max"}, context),
            };
        case DistributionType::Exponential:
            return DistributionSpec{
                DistributionType::Exponential,
                read_required_double(properties, {"_mean", "_exponentialMean"}, context),
                0.0,
            };
        case DistributionType::Normal:
            return DistributionSpec{
                DistributionType::Normal,
                read_required_double(properties, {"_mean"}, context),
                read_required_double(properties, {"_standardDeviation", "_stddev", "_std"}, context),
            };
        case DistributionType::LogNormal:
            return DistributionSpec{
                DistributionType::LogNormal,
                read_required_double(properties, {"_mean"}, context),
                read_required_double(properties, {"_standardDeviation", "_stddev", "_std"}, context),
            };
    }

    throw std::runtime_error(context + " uses unsupported distribution type.");
}

GeneratorSpec read_generator_spec(const pugi::xml_node& node)
{
    const auto properties = read_properties(node);
    const auto context = "Start event '" + read_required_attribute(node, "id", "Start event") + "'";

    GeneratorSpec generator;
    generator.interval_distribution = read_distribution(properties, "_initiatorType", context);
    generator.entity_count = read_required_count(properties, "_entityCount", context);
    generator.entity_type = read_required_text(properties, "_entityType", context);
    return generator;
}

TaskSpec read_task_spec(const pugi::xml_node& node)
{
    const auto properties = read_properties(node);
    const auto context = "Task '" + read_required_attribute(node, "id", "Task") + "'";

    if (const auto found = properties.find("_taskType"); found != properties.end())
    {
        const auto task_type = lower_copy(found->second);
        if (task_type != "delay")
        {
            throw std::runtime_error(context + " uses unsupported _taskType '" + found->second + "'. Only 'delay' is supported.");
        }
    }

    TaskSpec task;
    task.duration_distribution = read_distribution(properties, "_distributionType", context);
    if (const auto found = properties.find("_resourceStrategy"); found != properties.end())
    {
        task.resource_strategy = read_required_enum<ResourceStrategy>(properties, "_resourceStrategy", context);
    }
    else
    {
        task.resource_strategy = ResourceStrategy::None;
    }

    return task;
}

ResourceDefinition read_resource_definition(const pugi::xml_node& node)
{
    const auto properties = read_properties(node);
    const auto context = "Resource '" + read_required_attribute(node, "id", "Resource") + "'";

    if (const auto found = properties.find("_resourceType"); found != properties.end())
    {
        const auto resource_type = lower_copy(found->second);
        if (resource_type != "resource")
        {
            throw std::runtime_error(context + " uses unsupported _resourceType '" + found->second + "'.");
        }
    }

    ResourceDefinition definition;
    definition.id = read_required_attribute(node, "id", "Resource");
    definition.name = node.attribute("name").value();
    definition.capacity = static_cast<int>(read_required_count(properties, "_capacity", context));
    return definition;
}

} // namespace

SimulationModel BpmnParser::parse(const std::filesystem::path& input_path) const
{
    pugi::xml_document document;
    const auto path_text = input_path.string();
    const auto result = document.load_file(path_text.c_str());
    if (!result)
    {
        throw std::runtime_error("Failed to parse BPMN file '" + path_text + "': " + result.description());
    }

    const auto definitions = document.document_element();
    if (!definitions || local_name(definitions.name()) != "definitions")
    {
        throw std::runtime_error("Input file '" + path_text + "' is not a BPMN definitions document.");
    }

    pugi::xml_node process;
    for (const auto& child : definitions.children())
    {
        if (local_name(child.name()) == "process")
        {
            process = child;
            break;
        }
    }
    if (!process)
    {
        throw std::runtime_error("BPMN file '" + path_text + "' does not contain a process element.");
    }

    SimulationModel model;
    model.process_id = read_required_attribute(process, "id", "Process");
    model.process_name = process.attribute("name").value();

    std::vector<std::pair<std::string, std::string>> associations;

    for (const auto& child : process.children())
    {
        const auto type_name = local_name(child.name());
        if (type_name == "startEvent")
        {
            NodeDefinition definition;
            definition.id = read_required_attribute(child, "id", "Start event");
            definition.name = child.attribute("name").value();
            definition.type = NodeType::StartEvent;
            definition.generator = read_generator_spec(child);
            model.start_node_ids.push_back(definition.id);
            model.nodes.insert_or_assign(definition.id, std::move(definition));
            continue;
        }

        if (type_name == "task")
        {
            NodeDefinition definition;
            definition.id = read_required_attribute(child, "id", "Task");
            definition.name = child.attribute("name").value();
            definition.type = NodeType::Task;
            definition.task = read_task_spec(child);
            model.nodes.insert_or_assign(definition.id, std::move(definition));
            continue;
        }

        if (type_name == "endEvent")
        {
            NodeDefinition definition;
            definition.id = read_required_attribute(child, "id", "End event");
            definition.name = child.attribute("name").value();
            definition.type = NodeType::EndEvent;
            model.nodes.insert_or_assign(definition.id, std::move(definition));
            continue;
        }

        if (type_name == "exclusiveGateway")
        {
            NodeDefinition definition;
            definition.id = read_required_attribute(child, "id", "Exclusive gateway");
            definition.name = child.attribute("name").value();
            definition.type = NodeType::ExclusiveGateway;
            model.nodes.insert_or_assign(definition.id, std::move(definition));
            continue;
        }

        if (type_name == "parallelGateway")
        {
            NodeDefinition definition;
            definition.id = read_required_attribute(child, "id", "Parallel gateway");
            definition.name = child.attribute("name").value();
            definition.type = NodeType::ParallelGateway;
            model.nodes.insert_or_assign(definition.id, std::move(definition));
            continue;
        }

        if (type_name == "dataStoreReference")
        {
            auto resource = read_resource_definition(child);
            model.resources.insert_or_assign(resource.id, std::move(resource));
            continue;
        }

        if (type_name == "sequenceFlow")
        {
            SequenceFlowDefinition flow;
            flow.id = read_required_attribute(child, "id", "Sequence flow");
            flow.source_id = read_required_attribute(child, "sourceRef", "Sequence flow");
            flow.target_id = read_required_attribute(child, "targetRef", "Sequence flow");
            model.flows.push_back(std::move(flow));
            continue;
        }

        if (type_name == "association")
        {
            associations.emplace_back(
                read_required_attribute(child, "sourceRef", "Association"),
                read_required_attribute(child, "targetRef", "Association"));
        }
    }

    for (const auto& flow : model.flows)
    {
        model.outgoing[flow.source_id].push_back(flow.target_id);
        model.incoming[flow.target_id].push_back(flow.source_id);
    }

    for (const auto& [left_ref, right_ref] : associations)
    {
        const auto left_is_task = model.nodes.contains(left_ref) && model.node(left_ref).type == NodeType::Task;
        const auto right_is_task = model.nodes.contains(right_ref) && model.node(right_ref).type == NodeType::Task;
        const auto left_is_resource = model.resources.contains(left_ref);
        const auto right_is_resource = model.resources.contains(right_ref);

        if (left_is_task && right_is_resource)
        {
            model.task_resources[left_ref].push_back(right_ref);
        }
        else if (right_is_task && left_is_resource)
        {
            model.task_resources[right_ref].push_back(left_ref);
        }
    }

    for (auto& [task_id, resource_ids] : model.task_resources)
    {
        std::sort(resource_ids.begin(), resource_ids.end());
        resource_ids.erase(std::unique(resource_ids.begin(), resource_ids.end()), resource_ids.end());
    }

    std::sort(model.start_node_ids.begin(), model.start_node_ids.end());
    model.validate();
    return model;
}

} // namespace flux