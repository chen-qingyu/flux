#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <csv.hpp>
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

    [[nodiscard]] std::string require_text(const char* value, const std::string& name, const std::string& context) const
    {
        if (value != nullptr && *value != '\0')
        {
            return value;
        }
        throw std::runtime_error(context + " is missing required value '" + name + "'.");
    }

    [[nodiscard]] std::string require_text(const PropertyMap& properties, const std::string& key, const std::string& context) const
    {
        if (const auto found = properties.find(key); found != properties.end())
        {
            return require_text(found->second.c_str(), key, context);
        }
        throw std::runtime_error(context + " is missing required value '" + key + "'.");
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

    [[nodiscard]] std::vector<ExternalRecord> read_external_records(const std::string& start_id) const
    {
        const auto csv_path = std::filesystem::current_path() / "data" / "external" / (start_id + ".csv");
        if (!std::filesystem::exists(csv_path))
        {
            throw std::runtime_error("Start event '" + start_id + "' external csv file 'data/external/" + start_id + ".csv' was not found.");
        }

        csv::CSVReader reader(csv_path.string());

        const auto headers = reader.get_col_names();
        if (headers.empty() || headers.front() != "time")
        {
            throw std::runtime_error("Start event '" + start_id + "' external csv file must have 'time' as the first column header.");
        }

        std::vector<ExternalRecord> records;
        for (const auto& row : reader)
        {
            const auto parsed = row[0].get<double>();
            if (!std::isfinite(parsed) || parsed < 0.0)
            {
                throw std::runtime_error("Start event '" + start_id + "' external csv has invalid value.");
            }

            ExternalRecord record;
            record.time = parsed;
            for (std::size_t index = 1; index < headers.size(); ++index)
            {
                record.properties.insert_or_assign(headers[index], row[index].get<std::string>());
            }
            records.push_back(std::move(record));
        }

        std::stable_sort(records.begin(), records.end(), [](const ExternalRecord& left, const ExternalRecord& right)
                         { return left.time < right.time; });
        return records;
    }

    [[nodiscard]] double parse_double(const std::string& value, const std::string& name, const std::string& context) const
    {
        std::size_t parsed_size = 0;
        double parsed = 0.0;
        try
        {
            parsed = std::stod(value, &parsed_size);
        }
        catch (const std::exception&)
        {
            throw std::runtime_error(context + " value '" + name + "' must be a finite number.");
        }
        if (parsed_size != value.size() || !std::isfinite(parsed))
        {
            throw std::runtime_error(context + " value '" + name + "' must be a finite number.");
        }
        return parsed;
    }

    template <typename Enum>
    [[nodiscard]] Enum parse_enum(const std::string& value, const std::string& name, const std::string& context) const
    {
        if (const auto parsed = magic_enum::enum_cast<Enum>(value, magic_enum::case_insensitive); parsed.has_value())
        {
            return *parsed;
        }

        throw std::runtime_error(context + " value '" + name + "' is unsupported.");
    }

    [[nodiscard]] bool parse_bool(const std::string& value, const std::string& name, const std::string& context) const
    {
        if (value == "true")
        {
            return true;
        }
        if (value == "false")
        {
            return false;
        }

        throw std::runtime_error(context + " value '" + name + "' must be 'true' or 'false'.");
    }

    [[nodiscard]] std::size_t parse_count(const std::string& value, const std::string& name, const std::string& context, std::optional<std::size_t> maximum = std::nullopt) const
    {
        std::size_t parsed_size = 0;
        long long parsed = 0;
        try
        {
            parsed = std::stoll(value, &parsed_size);
        }
        catch (const std::exception&)
        {
            throw std::runtime_error(context + " value '" + name + "' must be greater than zero.");
        }
        if (parsed_size != value.size() || parsed <= 0)
        {
            throw std::runtime_error(context + " value '" + name + "' must be greater than zero.");
        }
        const auto count = static_cast<std::size_t>(parsed);
        if (maximum.has_value() && count > *maximum)
        {
            throw std::runtime_error(context + " value '" + name + "' exceeds supported maximum.");
        }
        return count;
    }

    [[nodiscard]] DistributionSpec read_distribution(const PropertyMap& properties, const std::string& type_key, const std::string& context) const
    {
        const auto require_number = [&](const std::string& key)
        {
            return parse_double(require_text(properties, key, context), key, context);
        };
        const auto distribution_type = parse_enum<DistributionType>(require_text(properties, type_key, context), type_key, context);
        DistributionSpec distribution;
        distribution.type = distribution_type;
        switch (distribution_type)
        {
            case DistributionType::Static:
                distribution.first = require_number("_staticInterval");
                distribution.second = 0.0;
                break;
            case DistributionType::Uniform:
                distribution.first = require_number("_min");
                distribution.second = require_number("_max");
                break;
            case DistributionType::Exponential:
                distribution.first = require_number("_mean");
                distribution.second = 0.0;
                break;
            case DistributionType::Normal:
                distribution.first = require_number("_mean");
                distribution.second = require_number("_standardDeviation");
                break;
            case DistributionType::LogNormal:
                distribution.first = require_number("_mean");
                distribution.second = require_number("_standardDeviation");
                break;
        }
        switch (distribution.type)
        {
            case DistributionType::Static:
                if (distribution.first < 0.0)
                {
                    throw std::runtime_error(context + " value '_staticInterval' must be non-negative.");
                }
                break;
            case DistributionType::Uniform:
                if (distribution.first < 0.0)
                {
                    throw std::runtime_error(context + " value '_min' must be non-negative.");
                }
                if (distribution.second < 0.0)
                {
                    throw std::runtime_error(context + " value '_max' must be non-negative.");
                }
                if (distribution.first > distribution.second)
                {
                    throw std::runtime_error(context + " value '_min' must be less than or equal to '_max'.");
                }
                break;
            case DistributionType::Exponential:
                if (distribution.first <= 0.0)
                {
                    throw std::runtime_error(context + " value '_mean' must be greater than zero.");
                }
                break;
            case DistributionType::Normal:
                if (distribution.first < 0.0)
                {
                    throw std::runtime_error(context + " value '_mean' must be non-negative.");
                }
                [[fallthrough]];
            case DistributionType::LogNormal:
                if (distribution.second <= 0.0)
                {
                    throw std::runtime_error(context + " value '_standardDeviation' must be greater than zero.");
                }
                break;
        }
        return distribution;
    }

    [[nodiscard]] SequenceFlowDefinition& flow_by_id(const std::string& flow_id)
    {
        if (const auto found = model_.flow_indexes.find(flow_id); found != model_.flow_indexes.end())
        {
            return model_.flows.at(found->second);
        }
        throw std::runtime_error("Unknown flow id: " + flow_id);
    }

    [[nodiscard]] std::size_t incoming_count(const std::string& node_id) const
    {
        if (const auto found = model_.incoming.find(node_id); found != model_.incoming.end())
        {
            return found->second.size();
        }
        return 0;
    }

    [[nodiscard]] std::size_t outgoing_count(const std::string& node_id) const
    {
        if (const auto found = model_.outgoing.find(node_id); found != model_.outgoing.end())
        {
            return found->second.size();
        }
        return 0;
    }

    [[nodiscard]] GeneratorSpec read_generator_spec(const pugi::xml_node& node) const
    {
        const auto properties = read_properties(node);
        const auto start_id = require_text(node.attribute("id").value(), "id", "Start event");
        const auto context = "Start event '" + start_id + "'";

        GeneratorSpec generator;
        generator.entity_type = require_text(properties, "_entityType", context);

        switch (parse_enum<InitiatorType>(require_text(properties, "_initiatorType", context), "_initiatorType", context))
        {
            case InitiatorType::Random:
                generator.type = InitiatorType::Random;
                generator.interval_distribution = read_distribution(properties, "_distributionType", context);
                generator.entity_count = parse_count(require_text(properties, "_entityCount", context), "_entityCount", context);
                return generator;
            case InitiatorType::External:
                generator.type = InitiatorType::External;
                generator.external_records = read_external_records(start_id);
                return generator;
        }
        return generator;
    }

    [[nodiscard]] CombineSpec read_combine_spec(const PropertyMap& properties, const std::string& context) const
    {
        CombineSpec combine;
        combine.method = parse_enum<CombineMethod>(require_text(properties, "_method", context), "_method", context);
        combine.use_quantity_property = parse_bool(require_text(properties, "_useQuantityProperty", context), "_useQuantityProperty", context);
        if (combine.use_quantity_property)
        {
            combine.quantity_property = require_text(properties, "_quantityProperty", context);
        }
        combine.ratio = parse_double(require_text(properties, "_ratio", context), "_ratio", context);
        if (combine.ratio <= 0.0)
        {
            throw std::runtime_error(context + " value '_ratio' must be greater than zero.");
        }
        if (combine.ratio < 1.0)
        {
            throw std::runtime_error(context + " value '_ratio' must be greater than or equal to 1.");
        }
        combine.entity_type = require_text(properties, "_entityType", context);

        switch (combine.method)
        {
            case CombineMethod::Ratio:
                return combine;
            case CombineMethod::GroupRatio:
                combine.group_property = require_text(properties, "_groupProperty", context);
                return combine;
        }

        throw std::runtime_error(context + " uses unsupported combine method.");
    }

    [[nodiscard]] SplitSpec read_split_spec(const PropertyMap& properties, const std::string& context) const
    {
        SplitSpec split;
        split.method = parse_enum<SplitMethod>(require_text(properties, "_method", context), "_method", context);
        split.one_off = parse_bool(require_text(properties, "_oneOff", context), "_oneOff", context);

        switch (split.method)
        {
            case SplitMethod::Ratio:
                split.ratio = parse_double(require_text(properties, "_ratio", context), "_ratio", context);
                if (split.ratio <= 0.0)
                {
                    throw std::runtime_error(context + " value '_ratio' must be greater than zero.");
                }
                split.entity_type = require_text(properties, "_entityType", context);
                return split;
            case SplitMethod::Restore:
                return split;
            case SplitMethod::Quantity:
                split.quantity_property = require_text(properties, "_quantityProperty", context);
                return split;
        }

        throw std::runtime_error(context + " uses unsupported split method.");
    }

    [[nodiscard]] TaskSpec read_task_spec(const pugi::xml_node& node) const
    {
        const auto properties = read_properties(node);
        const auto task_id = require_text(node.attribute("id").value(), "id", "Task");
        const auto context = "Task '" + task_id + "'";
        const auto declared_task_type = parse_enum<TaskType>(require_text(properties, "_taskType", context), "_taskType", context);
        TaskSpec task;

        task.type = declared_task_type;
        switch (task.type)
        {
            case TaskType::Delay:
                task.duration_distribution = read_distribution(properties, "_distributionType", context);
                break;
            case TaskType::Transport:
                task.distance = parse_double(require_text(properties, "_distance", context), "_distance", context);
                if (task.distance < 0.0)
                {
                    throw std::runtime_error(context + " value '_distance' must be non-negative.");
                }
                task.duration_distribution = read_distribution(properties, "_distributionType", context);
                break;
            case TaskType::AcquireResource:
            case TaskType::ReleaseResource:
                break;
            case TaskType::Combine:
                task.duration_distribution = read_distribution(properties, "_distributionType", context);
                task.combine = read_combine_spec(properties, context);
                break;
            case TaskType::Split:
                task.duration_distribution = read_distribution(properties, "_distributionType", context);
                task.split = read_split_spec(properties, context);
                break;
        }
        if (properties.contains("_resourceStrategy"))
        {
            task.resource_strategy = parse_enum<ResourceStrategy>(require_text(properties, "_resourceStrategy", context), "_resourceStrategy", context);
        }

        return task;
    }

    [[nodiscard]] ResourceDefinition read_resource_definition(const pugi::xml_node& node) const
    {
        const auto properties = read_properties(node);
        const auto resource_id = require_text(node.attribute("id").value(), "id", "Resource");
        const auto context = "Resource '" + resource_id + "'";

        const auto resource_type = require_text(properties, "_resourceType", context);
        if (resource_type != "resource")
        {
            throw std::runtime_error(context + " uses unsupported _resourceType '" + resource_type + "'.");
        }

        ResourceDefinition definition;
        definition.id = resource_id;
        definition.name = node.attribute("name").value();
        definition.capacity = static_cast<int>(parse_count(require_text(properties, "_capacity", context), "_capacity", context, static_cast<std::size_t>(std::numeric_limits<int>::max())));
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
        model_.process_id = require_text(process.attribute("id").value(), "id", "Process");
        model_.process_name = process.attribute("name").value();
    }

    void parse_process_children(const pugi::xml_node& process)
    {
        for (const auto& child : process.children())
        {
            const auto type_name = local_name(child.name());
            if (type_name == "startEvent")
            {
                parse_start_event(child);
            }
            else if (type_name == "task")
            {
                parse_task(child);
            }
            else if (type_name == "endEvent")
            {
                parse_end_event(child);
            }
            else if (type_name == "exclusiveGateway")
            {
                parse_exclusive_gateway(child);
            }
            else if (type_name == "dataStoreReference")
            {
                parse_resource(child);
            }
            else if (type_name == "sequenceFlow")
            {
                parse_sequence_flow(child);
            }
            else if (type_name == "association" || type_name == "dataInputAssociation" || type_name == "dataOutputAssociation")
            {
                parse_association(child);
            }
        }
    }

    void parse_start_event(const pugi::xml_node& child)
    {
        NodeDefinition definition;
        definition.id = require_text(child.attribute("id").value(), "id", "Start event");
        definition.name = child.attribute("name").value();
        definition.type = NodeType::StartEvent;
        definition.generator = read_generator_spec(child);
        model_.start_node_ids.push_back(definition.id);
        model_.nodes.insert_or_assign(definition.id, std::move(definition));
    }

    void parse_task(const pugi::xml_node& child)
    {
        NodeDefinition definition;
        definition.id = require_text(child.attribute("id").value(), "id", "Task");
        definition.name = child.attribute("name").value();
        definition.type = NodeType::Task;
        definition.task = read_task_spec(child);
        parse_task_data_output_associations(child, definition.id);
        model_.nodes.insert_or_assign(definition.id, std::move(definition));
    }

    void parse_end_event(const pugi::xml_node& child)
    {
        NodeDefinition definition;
        definition.id = require_text(child.attribute("id").value(), "id", "End event");
        definition.name = child.attribute("name").value();
        definition.type = NodeType::EndEvent;
        model_.nodes.insert_or_assign(definition.id, std::move(definition));
    }

    void parse_exclusive_gateway(const pugi::xml_node& child)
    {
        const auto properties = read_properties(child);
        NodeDefinition definition;
        definition.id = require_text(child.attribute("id").value(), "id", "Exclusive gateway");
        definition.name = child.attribute("name").value();
        definition.type = NodeType::ExclusiveGateway;
        if (const auto found = properties.find("_criteria"); found != properties.end() && !found->second.empty())
        {
            definition.gateway_criteria = parse_enum<GatewayCriteria>(found->second, "_criteria", "Exclusive gateway '" + definition.id + "'");
        }
        if (definition.gateway_criteria == GatewayCriteria::Property)
        {
            definition.gateway_property_name = require_text(properties, "_propertyName", "Exclusive gateway '" + definition.id + "'");
        }
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
        flow.id = require_text(child.attribute("id").value(), "id", "Sequence flow");
        flow.name = child.attribute("name").value();
        flow.source_id = require_text(child.attribute("sourceRef").value(), "sourceRef", "Sequence flow");
        flow.target_id = require_text(child.attribute("targetRef").value(), "targetRef", "Sequence flow");
        model_.flows.push_back(std::move(flow));
    }

    void parse_association(const pugi::xml_node& child)
    {
        associations_.emplace_back(
            require_text(child.attribute("sourceRef").value(), "sourceRef", "Association"),
            require_text(child.attribute("targetRef").value(), "targetRef", "Association"));
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
        // 顺序不能反：先建索引，再绑定资源，再归一化，最后才能解析权重并做一致性校验。
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
        // BPMN 中任务和资源的关联可能正反书写，这里统一折叠成 task -> resources。
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
            // 运行时依赖有序且去重后的资源列表来做稳定仲裁。
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

            const auto found = model_.outgoing_flow_ids.find(node_id);
            if (found == model_.outgoing_flow_ids.end() || found->second.empty())
            {
                continue;
            }

            for (const auto& flow_id : found->second)
            {
                auto& flow = flow_by_id(flow_id);
                if (definition.gateway_criteria == GatewayCriteria::Weight)
                {
                    flow.weight = parse_double(require_text(flow.name.c_str(), "name", "Sequence flow '" + flow.id + "'"), "name", "Sequence flow '" + flow.id + "'");
                    if (*flow.weight <= 0.0)
                    {
                        throw std::runtime_error("Sequence flow '" + flow.id + "' value 'name' must be greater than zero.");
                    }
                    flow.property_value.reset();
                    continue;
                }

                if (definition.gateway_criteria == GatewayCriteria::Property)
                {
                    flow.property_value = require_text(flow.name.c_str(), "name", "Sequence flow '" + flow.id + "'");
                    flow.weight.reset();
                }
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
                    if (definition.generator->type == InitiatorType::Random && definition.generator->entity_count == 0)
                    {
                        throw std::runtime_error("Start event '" + node_id + "' must generate at least one entity.");
                    }
                    if (definition.generator->type == InitiatorType::External && definition.generator->external_records.empty())
                    {
                        throw std::runtime_error("Start event '" + node_id + "' must generate at least one entity.");
                    }
                    if (incoming_count(node_id) != 0)
                    {
                        throw std::runtime_error("Start event '" + node_id + "' must not have incoming sequence flow.");
                    }
                    if (outgoing_count(node_id) != 1)
                    {
                        throw std::runtime_error("Start event '" + node_id + "' must have exactly one outgoing sequence flow.");
                    }
                    break;
                case NodeType::Task:
                    if (!definition.task.has_value())
                    {
                        throw std::runtime_error("Task '" + node_id + "' is missing duration settings.");
                    }
                    if (incoming_count(node_id) == 0)
                    {
                        throw std::runtime_error("Task '" + node_id + "' must have at least one incoming sequence flow.");
                    }
                    if (outgoing_count(node_id) != 1)
                    {
                        throw std::runtime_error("Task '" + node_id + "' must have exactly one outgoing sequence flow.");
                    }
                    {
                        std::size_t resource_count = 0;
                        if (const auto resources = model_.task_resources.find(node_id); resources != model_.task_resources.end())
                        {
                            resource_count = resources->second.size();
                        }

                        const auto require_resource_types =
                            definition.task->type == TaskType::Delay ||
                            definition.task->type == TaskType::Transport ||
                            definition.task->type == TaskType::Combine ||
                            definition.task->type == TaskType::Split ||
                            definition.task->type == TaskType::AcquireResource;

                        if (require_resource_types && resource_count > 1 && !definition.task->resource_strategy.has_value())
                        {
                            throw std::runtime_error("Task '" + node_id + "' must provide '_resourceStrategy' when multiple resources are associated.");
                        }

                        if (definition.task->type == TaskType::AcquireResource)
                        {
                            if (resource_count == 0)
                            {
                                throw std::runtime_error("Task '" + node_id + "' must bind at least one resource when '_taskType=acquireResource'.");
                            }
                        }
                    }
                    if (definition.task->type == TaskType::Combine && !definition.task->combine.has_value())
                    {
                        throw std::runtime_error("Task '" + node_id + "' is missing combine settings.");
                    }
                    if (definition.task->type == TaskType::Split && !definition.task->split.has_value())
                    {
                        throw std::runtime_error("Task '" + node_id + "' is missing split settings.");
                    }
                    break;
                case NodeType::EndEvent:
                    if (incoming_count(node_id) == 0)
                    {
                        throw std::runtime_error("End event '" + node_id + "' must have at least one incoming sequence flow.");
                    }
                    if (outgoing_count(node_id) != 0)
                    {
                        throw std::runtime_error("End event '" + node_id + "' must not have outgoing sequence flow.");
                    }
                    break;
                case NodeType::ExclusiveGateway:
                    if (!definition.gateway_criteria.has_value())
                    {
                        throw std::runtime_error("Exclusive gateway '" + node_id + "' must define '_criteria'.");
                    }
                    if (incoming_count(node_id) == 0)
                    {
                        throw std::runtime_error("Exclusive gateway '" + node_id + "' must have at least one incoming sequence flow.");
                    }
                    if (outgoing_count(node_id) == 0)
                    {
                        throw std::runtime_error("Exclusive gateway '" + node_id + "' must have at least one outgoing sequence flow.");
                    }
                    if (definition.gateway_criteria == GatewayCriteria::Weight)
                    {
                        const auto flow_ids = model_.outgoing_flow_ids.find(node_id);
                        for (const auto& flow_id : flow_ids->second)
                        {
                            const auto& flow = flow_by_id(flow_id);
                            if (!flow.weight.has_value())
                            {
                                throw std::runtime_error("Sequence flow '" + flow.id + "' must define a positive numeric weight in sequence flow name.");
                            }
                        }
                    }
                    if (definition.gateway_criteria == GatewayCriteria::Property)
                    {
                        if (!definition.gateway_property_name.has_value() || definition.gateway_property_name->empty())
                        {
                            throw std::runtime_error("Exclusive gateway '" + node_id + "' must define '_propertyName' when '_criteria=property'.");
                        }

                        std::unordered_set<std::string> property_values;
                        const auto flow_ids = model_.outgoing_flow_ids.find(node_id);
                        for (const auto& flow_id : flow_ids->second)
                        {
                            const auto& flow = flow_by_id(flow_id);
                            if (!flow.property_value.has_value() || flow.property_value->empty())
                            {
                                throw std::runtime_error("Sequence flow '" + flow.id + "' must define a non-empty property value in sequence flow name.");
                            }
                            if (!property_values.insert(*flow.property_value).second)
                            {
                                throw std::runtime_error("Exclusive gateway '" + node_id + "' has duplicate property branch name '" + *flow.property_value + "'.");
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

Model Parser::parse(const std::filesystem::path& file_path)
{
    ParseSession session;
    return session.parse(file_path);
}

} // namespace flux
