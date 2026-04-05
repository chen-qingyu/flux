#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <magic_enum/magic_enum.hpp>
#include <pugixml.hpp>

#include "tools.hpp"

namespace flux
{
class Parser::ParseSession
{
public:
    Model parse(const std::filesystem::path& file_path)
    {
        pugi::xml_document document;
        const auto path_text = file_path.string();
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

        const auto process = find_process(definitions, path_text);
        initialize_model(process);
        parse_process_children(process);
        finalize_model();
        return std::move(model_);
    }

private:
    using PropertyMap = std::unordered_map<std::string, std::string>;

    [[nodiscard]] std::string local_name(const char* raw_name) const
    {
        std::string name = raw_name == nullptr ? "" : raw_name;
        if (const auto separator = name.find(':'); separator != std::string::npos)
        {
            return name.substr(separator + 1);
        }
        return name;
    }

    [[nodiscard]] std::string lower_copy(std::string value) const
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        return value;
    }

    [[nodiscard]] std::string read_required_attribute(const pugi::xml_node& node, const char* attribute_name, const char* context) const
    {
        const auto attribute = node.attribute(attribute_name);
        if (!attribute)
        {
            throw std::runtime_error(std::string(context) + " is missing required attribute '" + attribute_name + "'.");
        }
        return attribute.value();
    }

    [[nodiscard]] PropertyMap read_properties(const pugi::xml_node& owner) const
    {
        PropertyMap properties;

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

    [[nodiscard]] double read_required_double(const PropertyMap& properties, const std::string& key, const std::string& context) const
    {
        if (const auto found = properties.find(key); found != properties.end())
        {
            std::size_t parsed_size = 0;
            const auto parsed = std::stod(found->second, &parsed_size);
            if (parsed_size != found->second.size() || !std::isfinite(parsed))
            {
                throw std::runtime_error(context + " property '" + key + "' must be a finite number.");
            }
            return parsed;
        }
        throw std::runtime_error(context + " is missing required numeric property '" + key + "'.");
    }

    [[nodiscard]] std::string read_required_text(const PropertyMap& properties, const std::string& key, const std::string& context) const
    {
        if (const auto found = properties.find(key); found != properties.end() && !found->second.empty())
        {
            return found->second;
        }
        throw std::runtime_error(context + " is missing required property '" + key + "'.");
    }

    template <typename Enum>
    [[nodiscard]] Enum read_required_enum(const PropertyMap& properties, const std::string& key, const std::string& context) const
    {
        const auto value = read_required_text(properties, key, context);
        if (const auto parsed = magic_enum::enum_cast<Enum>(value, magic_enum::case_insensitive); parsed.has_value())
        {
            return *parsed;
        }

        throw std::runtime_error(context + " uses unsupported property '" + key + "' value '" + value + "'.");
    }

    template <typename Enum>
    [[nodiscard]] std::optional<Enum> read_optional_enum(const PropertyMap& properties, const std::string& key, const std::string& context) const
    {
        const auto found = properties.find(key);
        if (found == properties.end() || found->second.empty())
        {
            return std::nullopt;
        }

        if (const auto parsed = magic_enum::enum_cast<Enum>(found->second, magic_enum::case_insensitive); parsed.has_value())
        {
            return *parsed;
        }

        throw std::runtime_error(context + " uses unsupported property '" + key + "' value '" + found->second + "'.");
    }

    [[nodiscard]] std::size_t read_required_count(const PropertyMap& properties, const std::string& key, const std::string& context) const
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

    [[nodiscard]] DistributionSpec read_distribution(const PropertyMap& properties, const std::string& type_key, const std::string& context) const
    {
        const auto distribution_type = read_required_enum<DistributionType>(properties, type_key, context);
        switch (distribution_type)
        {
            case DistributionType::Static:
                return DistributionSpec{DistributionType::Static, read_required_double(properties, "_staticInterval", context), 0.0};
            case DistributionType::Uniform:
                return DistributionSpec{
                    DistributionType::Uniform,
                    read_required_double(properties, "_min", context),
                    read_required_double(properties, "_max", context),
                };
            case DistributionType::Exponential:
                return DistributionSpec{
                    DistributionType::Exponential,
                    read_required_double(properties, "_mean", context),
                    0.0,
                };
            case DistributionType::Normal:
                return DistributionSpec{
                    DistributionType::Normal,
                    read_required_double(properties, "_mean", context),
                    read_required_double(properties, "_standardDeviation", context),
                };
            case DistributionType::LogNormal:
                return DistributionSpec{
                    DistributionType::LogNormal,
                    read_required_double(properties, "_mean", context),
                    read_required_double(properties, "_standardDeviation", context),
                };
        }

        throw std::runtime_error(context + " uses unsupported distribution type.");
    }

    [[nodiscard]] std::optional<GatewayCriteria> read_optional_gateway_criteria(const PropertyMap& properties, const std::string& context) const
    {
        const auto found = properties.find("_criteria");
        if (found == properties.end() || found->second.empty())
        {
            return std::nullopt;
        }

        if (lower_copy(found->second) == "by_weight")
        {
            return GatewayCriteria::ByWeight;
        }

        throw std::runtime_error(context + " uses unsupported property '_criteria' value '" + found->second + "'.");
    }

    [[nodiscard]] double parse_required_positive_double(const std::string& value, const std::string& context) const
    {
        if (value.empty())
        {
            throw std::runtime_error(context + " must define a positive numeric weight in sequence flow name.");
        }

        std::size_t parsed_size = 0;
        const auto parsed = std::stod(value, &parsed_size);
        if (parsed_size != value.size() || !std::isfinite(parsed) || parsed <= 0.0)
        {
            throw std::runtime_error(context + " must define a positive numeric weight in sequence flow name.");
        }
        return parsed;
    }

    [[nodiscard]] SequenceFlowDefinition& flow_by_id(const std::string& flow_id)
    {
        if (const auto found = model_.flow_indexes.find(flow_id); found != model_.flow_indexes.end())
        {
            return model_.flows.at(found->second);
        }
        throw std::runtime_error("Unknown flow id: " + flow_id);
    }

    void validate_distribution(const DistributionSpec& distribution, const std::string& context) const
    {
        switch (distribution.type)
        {
            case DistributionType::Static:
                if (distribution.first < 0.0)
                {
                    throw std::runtime_error(context + " property '_staticInterval' must be non-negative.");
                }
                break;
            case DistributionType::Uniform:
                if (distribution.first < 0.0)
                {
                    throw std::runtime_error(context + " property '_min' must be non-negative.");
                }
                if (distribution.second < 0.0)
                {
                    throw std::runtime_error(context + " property '_max' must be non-negative.");
                }
                if (distribution.first > distribution.second)
                {
                    throw std::runtime_error(context + " property '_min' must be less than or equal to '_max'.");
                }
                break;
            case DistributionType::Exponential:
                if (distribution.first <= 0.0)
                {
                    throw std::runtime_error(context + " property '_mean' must be greater than zero.");
                }
                break;
            case DistributionType::Normal:
                if (distribution.first < 0.0)
                {
                    throw std::runtime_error(context + " property '_mean' must be non-negative.");
                }
                [[fallthrough]];
            case DistributionType::LogNormal:
                if (distribution.second <= 0.0)
                {
                    throw std::runtime_error(context + " property '_standardDeviation' must be greater than zero.");
                }
                break;
        }
    }

    [[nodiscard]] GeneratorSpec read_generator_spec(const pugi::xml_node& node) const
    {
        const auto properties = read_properties(node);
        const auto context = "Start event '" + read_required_attribute(node, "id", "Start event") + "'";

        const auto initiator_type = lower_copy(read_required_text(properties, "_initiatorType", context));
        if (initiator_type != "random")
        {
            throw std::runtime_error(context + " uses unsupported _initiatorType '" + initiator_type + "'. Only 'random' is supported.");
        }

        GeneratorSpec generator;
        generator.interval_distribution = read_distribution(properties, "_distributionType", context);
        generator.entity_count = read_required_count(properties, "_entityCount", context);
        generator.entity_type = read_required_text(properties, "_entityType", context);
        return generator;
    }

    [[nodiscard]] TaskSpec read_task_spec(const pugi::xml_node& node) const
    {
        const auto properties = read_properties(node);
        const auto context = "Task '" + read_required_attribute(node, "id", "Task") + "'";

        const auto task_type = lower_copy(read_required_text(properties, "_taskType", context));
        TaskSpec task;
        if (task_type == "delay")
        {
            task.type = TaskType::Delay;
        }
        else if (task_type == "transport")
        {
            task.type = TaskType::Transport;
            task.distance = read_required_double(properties, "_distance", context);
            if (task.distance < 0.0)
            {
                throw std::runtime_error(context + " property '_distance' must be non-negative.");
            }
        }
        else if (task_type == "acquireresource")
        {
            task.type = TaskType::AcquireResource;
        }
        else if (task_type == "releaseresource")
        {
            task.type = TaskType::ReleaseResource;
        }
        else
        {
            throw std::runtime_error(context + " uses unsupported _taskType '" + task_type + "'. Only 'delay', 'transport', 'acquireResource', and 'releaseResource' are supported.");
        }

        if (task.type == TaskType::Delay || task.type == TaskType::Transport)
        {
            task.duration_distribution = read_distribution(properties, "_distributionType", context);
        }
        if (properties.contains("_resourceStrategy"))
        {
            if (task.type == TaskType::ReleaseResource)
            {
                throw std::runtime_error(context + " must not define '_resourceStrategy'.");
            }
            task.resource_strategy = read_required_enum<ResourceStrategy>(properties, "_resourceStrategy", context);
        }

        return task;
    }

    [[nodiscard]] ResourceDefinition read_resource_definition(const pugi::xml_node& node) const
    {
        const auto properties = read_properties(node);
        const auto context = "Resource '" + read_required_attribute(node, "id", "Resource") + "'";

        const auto resource_type = lower_copy(read_required_text(properties, "_resourceType", context));
        if (resource_type != "resource")
        {
            throw std::runtime_error(context + " uses unsupported _resourceType '" + resource_type + "'.");
        }

        ResourceDefinition definition;
        definition.id = read_required_attribute(node, "id", "Resource");
        definition.name = node.attribute("name").value();
        definition.capacity = static_cast<int>(read_required_count(properties, "_capacity", context));
        return definition;
    }

    [[nodiscard]] pugi::xml_node find_process(const pugi::xml_node& definitions, const std::string& path_text) const
    {
        for (const auto& child : definitions.children())
        {
            if (local_name(child.name()) == "process")
            {
                return child;
            }
        }

        throw std::runtime_error("BPMN file '" + path_text + "' does not contain a process element.");
    }

    void initialize_model(const pugi::xml_node& process)
    {
        model_.process_id = read_required_attribute(process, "id", "Process");
        model_.process_name = process.attribute("name").value();
    }

    void parse_process_children(const pugi::xml_node& process)
    {
        for (const auto& child : process.children())
        {
            parse_process_child(child);
        }
    }

    void parse_process_child(const pugi::xml_node& child)
    {
        const auto type_name = local_name(child.name());
        if (type_name == "startEvent")
        {
            parse_start_event(child);
            return;
        }
        if (type_name == "task")
        {
            parse_task(child);
            return;
        }
        if (type_name == "acquireResourceTask")
        {
            parse_task(child);
            return;
        }
        if (type_name == "releaseResourceTask")
        {
            parse_task(child);
            return;
        }
        if (type_name == "transportTask")
        {
            parse_task(child);
            return;
        }
        if (type_name == "endEvent")
        {
            parse_end_event(child);
            return;
        }
        if (type_name == "exclusiveGateway")
        {
            parse_exclusive_gateway(child);
            return;
        }
        if (type_name == "parallelGateway")
        {
            parse_parallel_gateway(child);
            return;
        }
        if (type_name == "dataStoreReference")
        {
            parse_resource(child);
            return;
        }
        if (type_name == "sequenceFlow")
        {
            parse_sequence_flow(child);
            return;
        }
        if (type_name == "association")
        {
            parse_association(child);
            return;
        }
        if (type_name == "dataInputAssociation" || type_name == "dataOutputAssociation")
        {
            parse_association(child);
        }
    }

    void parse_start_event(const pugi::xml_node& child)
    {
        NodeDefinition definition;
        definition.id = read_required_attribute(child, "id", "Start event");
        definition.name = child.attribute("name").value();
        definition.type = NodeType::StartEvent;
        definition.generator = read_generator_spec(child);
        model_.start_node_ids.push_back(definition.id);
        model_.nodes.insert_or_assign(definition.id, std::move(definition));
    }

    void parse_task(const pugi::xml_node& child)
    {
        NodeDefinition definition;
        definition.id = read_required_attribute(child, "id", "Task");
        definition.name = child.attribute("name").value();
        definition.type = NodeType::Task;
        definition.task = read_task_spec(child);
        parse_task_data_output_associations(child, definition.id);
        model_.nodes.insert_or_assign(definition.id, std::move(definition));
    }

    void parse_end_event(const pugi::xml_node& child)
    {
        NodeDefinition definition;
        definition.id = read_required_attribute(child, "id", "End event");
        definition.name = child.attribute("name").value();
        definition.type = NodeType::EndEvent;
        model_.nodes.insert_or_assign(definition.id, std::move(definition));
    }

    void parse_exclusive_gateway(const pugi::xml_node& child)
    {
        const auto properties = read_properties(child);
        NodeDefinition definition;
        definition.id = read_required_attribute(child, "id", "Exclusive gateway");
        definition.name = child.attribute("name").value();
        definition.type = NodeType::ExclusiveGateway;
        definition.gateway_criteria = read_optional_gateway_criteria(properties, "Exclusive gateway '" + definition.id + "'");
        model_.nodes.insert_or_assign(definition.id, std::move(definition));
    }

    void parse_parallel_gateway(const pugi::xml_node& child)
    {
        NodeDefinition definition;
        definition.id = read_required_attribute(child, "id", "Parallel gateway");
        definition.name = child.attribute("name").value();
        definition.type = NodeType::ParallelGateway;
        model_.nodes.insert_or_assign(definition.id, std::move(definition));
    }

    void parse_resource(const pugi::xml_node& child)
    {
        auto resource = read_resource_definition(child);
        model_.resources.insert_or_assign(resource.id, std::move(resource));
    }

    void parse_sequence_flow(const pugi::xml_node& child)
    {
        SequenceFlowDefinition flow;
        flow.id = read_required_attribute(child, "id", "Sequence flow");
        flow.name = child.attribute("name").value();
        flow.source_id = read_required_attribute(child, "sourceRef", "Sequence flow");
        flow.target_id = read_required_attribute(child, "targetRef", "Sequence flow");
        model_.flows.push_back(std::move(flow));
    }

    void parse_association(const pugi::xml_node& child)
    {
        associations_.emplace_back(
            read_required_attribute(child, "sourceRef", "Association"),
            read_required_attribute(child, "targetRef", "Association"));
    }

    void parse_task_data_output_associations(const pugi::xml_node& task_node, const std::string& task_id)
    {
        for (const auto& child : task_node.children())
        {
            if (local_name(child.name()) != "dataOutputAssociation")
            {
                continue;
            }

            const auto context = "Task '" + task_id + "' dataOutputAssociation";
            const auto target_ref = find_association_target_ref(child, context);
            associations_.emplace_back(task_id, target_ref);
        }
    }

    [[nodiscard]] std::string find_association_target_ref(const pugi::xml_node& association_node, const std::string& context) const
    {
        for (const auto& child : association_node.children())
        {
            if (local_name(child.name()) != "targetRef")
            {
                continue;
            }

            return child.child_value();
        }

        throw std::runtime_error(context + " is missing targetRef.");
    }

    void finalize_model()
    {
        build_flow_indexes();
        bind_task_resources();
        normalize_model();
        resolve_splitter_weights();
        validate_model();
    }

    void build_flow_indexes()
    {
        for (std::size_t index = 0; index < model_.flows.size(); ++index)
        {
            const auto& flow = model_.flows[index];
            model_.outgoing[flow.source_id].push_back(flow.target_id);
            model_.incoming[flow.target_id].push_back(flow.source_id);
            model_.outgoing_flow_ids[flow.source_id].push_back(flow.id);
            model_.flow_indexes.insert_or_assign(flow.id, index);
        }
    }

    void bind_task_resources()
    {
        for (const auto& [left_ref, right_ref] : associations_)
        {
            const auto left_is_task = model_.nodes.contains(left_ref) && flux::node(model_, left_ref).type == NodeType::Task;
            const auto right_is_task = model_.nodes.contains(right_ref) && flux::node(model_, right_ref).type == NodeType::Task;
            const auto left_is_resource = model_.resources.contains(left_ref);
            const auto right_is_resource = model_.resources.contains(right_ref);

            if (left_is_task && right_is_resource)
            {
                model_.task_resources[left_ref].push_back(right_ref);
            }
            else if (right_is_task && left_is_resource)
            {
                model_.task_resources[right_ref].push_back(left_ref);
            }
        }
    }

    void normalize_model()
    {
        for (auto& [task_id, resource_ids] : model_.task_resources)
        {
            std::sort(resource_ids.begin(), resource_ids.end());
            resource_ids.erase(std::unique(resource_ids.begin(), resource_ids.end()), resource_ids.end());
        }

        std::sort(model_.start_node_ids.begin(), model_.start_node_ids.end());
    }

    void resolve_splitter_weights()
    {
        for (const auto& [node_id, definition] : model_.nodes)
        {
            if (definition.type != NodeType::ExclusiveGateway || !definition.gateway_criteria.has_value())
            {
                continue;
            }

            if (*definition.gateway_criteria != GatewayCriteria::ByWeight)
            {
                throw std::runtime_error("Exclusive gateway '" + node_id + "' uses unsupported routing criteria.");
            }

            const auto found = model_.outgoing_flow_ids.find(node_id);
            if (found == model_.outgoing_flow_ids.end() || found->second.empty())
            {
                continue;
            }

            for (const auto& flow_id : found->second)
            {
                auto& flow = flow_by_id(flow_id);
                flow.weight = parse_required_positive_double(flow.name, "Sequence flow '" + flow.id + "'");
            }
        }
    }

    void validate_model()
    {
        if (model_.process_id.empty())
        {
            throw std::runtime_error("BPMN process id is missing.");
        }
        if (model_.start_node_ids.empty())
        {
            throw std::runtime_error("Simulation model must contain at least one start event.");
        }

        for (const auto& [node_id, definition] : model_.nodes)
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
                    if (!model_.outgoing.contains(node_id) || model_.outgoing.at(node_id).empty())
                    {
                        throw std::runtime_error("Start event '" + node_id + "' must have outgoing sequence flow.");
                    }
                    validate_distribution(definition.generator->interval_distribution, "Start event '" + node_id + "'");
                    break;
                case NodeType::Task:
                    if (!definition.task.has_value())
                    {
                        throw std::runtime_error("Task '" + node_id + "' is missing duration settings.");
                    }
                    if (!model_.outgoing.contains(node_id) || model_.outgoing.at(node_id).empty())
                    {
                        throw std::runtime_error("Task '" + node_id + "' must have outgoing sequence flow.");
                    }
                    {
                        std::size_t resource_count = 0;
                        if (const auto resources = model_.task_resources.find(node_id); resources != model_.task_resources.end())
                        {
                            resource_count = resources->second.size();
                        }

                        if ((definition.task->type == TaskType::Delay || definition.task->type == TaskType::Transport) && resource_count > 1 && !definition.task->resource_strategy.has_value())
                        {
                            throw std::runtime_error("Task '" + node_id + "' must provide '_resourceStrategy' when multiple resources are associated.");
                        }

                        if (definition.task->type == TaskType::AcquireResource)
                        {
                            if (resource_count == 0)
                            {
                                throw std::runtime_error("Task '" + node_id + "' must bind at least one resource when '_taskType=acquireResource'.");
                            }
                            if (resource_count > 1 && !definition.task->resource_strategy.has_value())
                            {
                                throw std::runtime_error("Task '" + node_id + "' must provide '_resourceStrategy' when multiple resources are associated.");
                            }
                        }

                        if (definition.task->type == TaskType::ReleaseResource && definition.task->resource_strategy.has_value())
                        {
                            throw std::runtime_error("Task '" + node_id + "' must not define '_resourceStrategy'.");
                        }
                    }
                    if (definition.task->type == TaskType::Delay || definition.task->type == TaskType::Transport)
                    {
                        validate_distribution(definition.task->duration_distribution, "Task '" + node_id + "'");
                    }
                    break;
                case NodeType::EndEvent:
                    break;
                case NodeType::ExclusiveGateway:
                    if (!definition.gateway_criteria.has_value())
                    {
                        throw std::runtime_error("Exclusive gateway '" + node_id + "' must define '_criteria'.");
                    }
                    if (*definition.gateway_criteria != GatewayCriteria::ByWeight)
                    {
                        throw std::runtime_error("Exclusive gateway '" + node_id + "' uses unsupported routing criteria.");
                    }
                    [[fallthrough]];
                case NodeType::ParallelGateway:
                    if (!model_.outgoing.contains(node_id) || model_.outgoing.at(node_id).empty())
                    {
                        throw std::runtime_error("Gateway '" + node_id + "' must have outgoing sequence flow.");
                    }
                    if (definition.type == NodeType::ExclusiveGateway && definition.gateway_criteria == GatewayCriteria::ByWeight)
                    {
                        const auto flow_ids = model_.outgoing_flow_ids.find(node_id);
                        if (flow_ids == model_.outgoing_flow_ids.end() || flow_ids->second.empty())
                        {
                            throw std::runtime_error("Exclusive gateway '" + node_id + "' must have outgoing sequence flow.");
                        }
                        for (const auto& flow_id : flow_ids->second)
                        {
                            const auto& flow = flow_by_id(flow_id);
                            if (!flow.weight.has_value())
                            {
                                throw std::runtime_error("Sequence flow '" + flow.id + "' must define a positive numeric weight in sequence flow name.");
                            }
                        }
                    }
                    break;
            }
        }

        for (const auto& flow : model_.flows)
        {
            if (!model_.nodes.contains(flow.source_id))
            {
                throw std::runtime_error("Sequence flow '" + flow.id + "' has unknown source node '" + flow.source_id + "'.");
            }
            if (!model_.nodes.contains(flow.target_id))
            {
                throw std::runtime_error("Sequence flow '" + flow.id + "' has unknown target node '" + flow.target_id + "'.");
            }
        }

        for (const auto& [task_id, resource_ids] : model_.task_resources)
        {
            if (!model_.nodes.contains(task_id))
            {
                throw std::runtime_error("Task-resource binding references unknown task '" + task_id + "'.");
            }
            for (const auto& resource_id : resource_ids)
            {
                if (!model_.resources.contains(resource_id))
                {
                    throw std::runtime_error("Task-resource binding references unknown resource '" + resource_id + "'.");
                }
            }
        }
    }

    Model model_;
    std::vector<std::pair<std::string, std::string>> associations_;
};

Model Parser::parse(const std::filesystem::path& file_path) const
{
    ParseSession session;
    return session.parse(file_path);
}

} // namespace flux