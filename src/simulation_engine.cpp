#include "simulation_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <entt/entt.hpp>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace flux
{
namespace
{

enum class ScheduledEventType
{
    GenerateEntity,
    ArriveNode,
    FinishTask,
    ReevaluatePending,
};

struct ProcessToken
{
    std::string entity_id;
    std::string entity_type;
    std::string token_id;
    double created_at{0.0};
};

struct ActiveTask
{
    std::string task_id;
    double arrival_time{0.0};
    double start_time{0.0};
    std::vector<std::string> allocated_resources;
};

struct ResourceRuntime
{
    std::string resource_id;
    std::string resource_name;
    int capacity{0};
    int in_use{0};
    double last_update_time{0.0};
    double busy_unit_time{0.0};
    int max_queue_length{0};
    double total_wait_time{0.0};
    std::size_t allocation_count{0};
};

struct PendingTaskRequest
{
    std::uint64_t order{0};
    entt::entity token{entt::null};
    std::string task_id;
    double arrival_time{0.0};
};

struct ScheduledEvent
{
    double time{0.0};
    std::uint64_t order{0};
    ScheduledEventType type{ScheduledEventType::GenerateEntity};
    std::string node_id;
    entt::entity token{entt::null};
};

struct ScheduledEventCompare
{
    bool operator()(const ScheduledEvent& left, const ScheduledEvent& right) const
    {
        if (left.time != right.time)
        {
            return left.time > right.time;
        }
        return left.order > right.order;
    }
};

struct JoinBarrierState
{
    std::vector<entt::entity> waiting_tokens;
};

std::string json_escape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char ch : value)
    {
        switch (ch)
        {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::string json_string(const std::string& value)
{
    return "\"" + json_escape(value) + "\"";
}

std::string json_array(const std::vector<std::string>& values)
{
    std::string result = "[";
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (index > 0)
        {
            result += ',';
        }
        result += json_string(values[index]);
    }
    result += ']';
    return result;
}

std::string join_key(const std::string& entity_id, const std::string& node_id)
{
    return entity_id + "::" + node_id;
}

class DistributionSampler
{
public:
    explicit DistributionSampler(std::uint64_t seed)
        : generator_(seed)
    {
    }

    double sample_positive(const DistributionSpec& spec)
    {
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const auto value = sample(spec);
            if (value > 0.0)
            {
                return value;
            }
        }
        return 0.001;
    }

private:
    double sample(const DistributionSpec& spec)
    {
        switch (spec.type)
        {
            case DistributionType::Static:
                return spec.first;
            case DistributionType::Uniform:
            {
                std::uniform_real_distribution<double> distribution(spec.first, spec.second);
                return distribution(generator_);
            }
            case DistributionType::Exponential:
            {
                std::exponential_distribution<double> distribution(1.0 / spec.first);
                return distribution(generator_);
            }
            case DistributionType::Normal:
            {
                std::normal_distribution<double> distribution(spec.first, spec.second);
                return distribution(generator_);
            }
            case DistributionType::LogNormal:
            {
                std::lognormal_distribution<double> distribution(spec.first, spec.second);
                return distribution(generator_);
            }
        }

        return 0.0;
    }

    std::mt19937_64 generator_;
};

class RuntimeContext
{
public:
    RuntimeContext(const SimulationModel& model, SimulationResult& result, std::uint64_t seed)
        : model_(model)
        , result_(result)
        , sampler_(seed)
    {
        initialize_resources();
    }

    void schedule(ScheduledEvent event)
    {
        queue_.push(std::move(event));
    }

    [[nodiscard]] bool has_events() const
    {
        return !queue_.empty();
    }

    ScheduledEvent next_event()
    {
        auto event = queue_.top();
        queue_.pop();
        current_time_ = std::max(current_time_, event.time);
        result_.simulation_horizon = std::max(result_.simulation_horizon, current_time_);
        return event;
    }

    [[nodiscard]] std::uint64_t next_order()
    {
        return next_order_++;
    }

    [[nodiscard]] std::string next_entity_id(const std::string& start_name, const std::string& entity_type)
    {
        const auto index = business_sequence_++;
        return start_name + "_" + entity_type + "_" + std::to_string(index);
    }

    entt::entity create_token(const std::string& entity_id, const std::string& entity_type, const std::string& token_id, double created_at)
    {
        const auto entity = registry_.create();
        registry_.emplace<ProcessToken>(entity, ProcessToken{entity_id, entity_type, token_id, created_at});
        return entity;
    }

    [[nodiscard]] bool token_valid(entt::entity token) const
    {
        return token != entt::null && registry_.valid(token) && registry_.all_of<ProcessToken>(token);
    }

    [[nodiscard]] const ProcessToken& token(entt::entity entity) const
    {
        return registry_.get<ProcessToken>(entity);
    }

    ProcessToken& token(entt::entity entity)
    {
        return registry_.get<ProcessToken>(entity);
    }

    void destroy_token(entt::entity entity)
    {
        if (registry_.valid(entity))
        {
            registry_.destroy(entity);
        }
    }

    double sample_positive(const DistributionSpec& spec)
    {
        return sampler_.sample_positive(spec);
    }

    void log_event(double time, const ProcessToken& token_component, const NodeDefinition& node, const std::string& event_type, std::string resource_snapshot, std::string details_json)
    {
        result_.reports.event_rows.push_back(EventLogRow{
            time,
            token_component.entity_id,
            token_component.entity_type,
            node.id,
            node.name,
            to_string(node.type),
            event_type,
            std::move(resource_snapshot),
            std::move(details_json),
        });
    }

    void log_resource_timeline(double time, const ResourceRuntime& runtime, const std::string& change_type, int queue_length, const std::string& entity_id, const std::string& task_id, std::string details_json)
    {
        result_.reports.resource_timeline_rows.push_back(ResourceTimelineRow{
            time,
            runtime.resource_id,
            runtime.resource_name,
            change_type,
            runtime.in_use,
            runtime.capacity - runtime.in_use,
            queue_length,
            entity_id,
            task_id,
            std::move(details_json),
        });
    }

    [[nodiscard]] const SimulationModel& model() const
    {
        return model_;
    }

    [[nodiscard]] entt::registry& registry()
    {
        return registry_;
    }

    [[nodiscard]] std::unordered_map<std::string, JoinBarrierState>& join_barriers()
    {
        return join_barriers_;
    }

    [[nodiscard]] std::vector<std::string> task_resources(const std::string& task_id) const
    {
        if (const auto found = model_.task_resources.find(task_id); found != model_.task_resources.end())
        {
            return found->second;
        }
        return {};
    }

    [[nodiscard]] std::vector<std::string> resource_names(const std::vector<std::string>& resource_ids) const
    {
        std::vector<std::string> names;
        names.reserve(resource_ids.size());
        for (const auto& resource_id : resource_ids)
        {
            names.push_back(model_.resource(resource_id).name);
        }
        return names;
    }

    [[nodiscard]] int queue_length_for_resource(const std::string& resource_id) const
    {
        int count = 0;
        for (const auto& request : pending_requests_)
        {
            if (!token_valid(request.token))
            {
                continue;
            }
            const auto required = task_resources(request.task_id);
            if (std::find(required.begin(), required.end(), resource_id) != required.end())
            {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::size_t pending_request_count() const
    {
        return pending_requests_.size();
    }

    [[nodiscard]] std::string queue_lengths_json(const std::vector<std::string>& resource_ids) const
    {
        std::string result = "{";
        for (std::size_t index = 0; index < resource_ids.size(); ++index)
        {
            if (index > 0)
            {
                result += ',';
            }
            result += json_string(model_.resource(resource_ids[index]).name);
            result += ':';
            result += std::to_string(queue_length_for_resource(resource_ids[index]));
        }
        result += '}';
        return result;
    }

    [[nodiscard]] std::string resource_snapshot(const std::string& task_id, const std::vector<std::string>& allocation) const
    {
        const auto requested = task_resources(task_id);
        return "{\"requested\":" + json_array(resource_names(requested)) + ",\"allocated\":" + json_array(resource_names(allocation)) + ",\"queue_lengths\":" + queue_lengths_json(requested) + '}';
    }

    [[nodiscard]] std::vector<std::string> allocate_resources_if_possible(const std::string& task_id, ResourceStrategy strategy)
    {
        const auto resource_ids = task_resources(task_id);
        if (resource_ids.empty())
        {
            return {};
        }

        if (strategy == ResourceStrategy::All)
        {
            for (const auto& resource_id : resource_ids)
            {
                const auto& runtime = resource_runtime(resource_id);
                if (runtime.in_use >= runtime.capacity)
                {
                    return {};
                }
            }
            return resource_ids;
        }

        if (strategy == ResourceStrategy::Any)
        {
            for (const auto& resource_id : resource_ids)
            {
                const auto& runtime = resource_runtime(resource_id);
                if (runtime.in_use < runtime.capacity)
                {
                    return {resource_id};
                }
            }
        }

        return {};
    }

    void apply_allocation(const std::vector<std::string>& resource_ids, double time, double wait_time, const std::string& entity_id, const std::string& task_id)
    {
        for (const auto& resource_id : resource_ids)
        {
            auto& runtime = resource_runtime(resource_id);
            update_busy_time(runtime, time);
            ++runtime.in_use;
            ++runtime.allocation_count;
            runtime.total_wait_time += wait_time;
            runtime.max_queue_length = std::max(runtime.max_queue_length, queue_length_for_resource(resource_id));
            log_resource_timeline(
                time,
                runtime,
                "allocate",
                queue_length_for_resource(resource_id),
                entity_id,
                task_id,
                "{\"wait_time\":" + format_time(wait_time) + ",\"entity_id\":" + json_string(entity_id) + "}");
        }
    }

    void apply_release(const std::vector<std::string>& resource_ids, double time, const std::string& entity_id, const std::string& task_id)
    {
        for (const auto& resource_id : resource_ids)
        {
            auto& runtime = resource_runtime(resource_id);
            update_busy_time(runtime, time);
            runtime.in_use = std::max(0, runtime.in_use - 1);
            runtime.max_queue_length = std::max(runtime.max_queue_length, queue_length_for_resource(resource_id));
            log_resource_timeline(
                time,
                runtime,
                "release",
                queue_length_for_resource(resource_id),
                entity_id,
                task_id,
                "{\"entity_id\":" + json_string(entity_id) + "}");
        }
    }

    void enqueue_request(PendingTaskRequest request)
    {
        pending_requests_.push_back(std::move(request));
        refresh_all_queue_lengths();
    }

    void reevaluate_pending(double time)
    {
        bool progress = true;
        while (progress)
        {
            progress = false;
            for (auto it = pending_requests_.begin(); it != pending_requests_.end();)
            {
                if (!token_valid(it->token))
                {
                    it = pending_requests_.erase(it);
                    refresh_all_queue_lengths();
                    continue;
                }

                const auto& node = model_.node(it->task_id);
                const auto required = task_resources(it->task_id);
                const auto allocation = allocate_resources_if_possible(it->task_id, node.task->resource_strategy);
                if (!required.empty() && allocation.empty())
                {
                    ++it;
                    continue;
                }

                const auto token_entity = it->token;
                const auto arrival_time = it->arrival_time;
                it = pending_requests_.erase(it);
                refresh_all_queue_lengths();
                start_task(token_entity, node, time, allocation, time - arrival_time);
                progress = true;
                break;
            }
        }
    }

    void start_task(entt::entity token_entity, const NodeDefinition& node, double time, const std::vector<std::string>& allocation, double wait_time)
    {
        auto& token_component = token(token_entity);
        apply_allocation(allocation, time, wait_time, token_component.entity_id, node.id);
        registry_.emplace_or_replace<ActiveTask>(token_entity, ActiveTask{node.id, time - wait_time, time, allocation});

        const auto duration = sample_positive(node.task->duration_distribution);
        log_event(
            time,
            token_component,
            node,
            "task_start",
            resource_snapshot(node.id, allocation),
            "{\"wait_time\":" + format_time(wait_time) + ",\"duration\":" + format_time(duration) + ",\"allocated_resources\":" + json_array(resource_names(allocation)) + "}");

        schedule(ScheduledEvent{time + duration, next_order(), ScheduledEventType::FinishTask, node.id, token_entity});
    }

    void finalize_resources(double horizon_s)
    {
        for (const auto& resource_id : resource_ids_)
        {
            auto& runtime = resource_runtime(resource_id);
            update_busy_time(runtime, horizon_s);

            const auto capacity_time = static_cast<double>(runtime.capacity) * horizon_s;
            const auto busy_time = runtime.busy_unit_time;
            const auto idle_time = std::max(0.0, capacity_time - busy_time);
            const auto utilization = capacity_time > 0.0 ? busy_time / capacity_time : 0.0;
            const auto average_wait_time = runtime.allocation_count > 0 ? runtime.total_wait_time / static_cast<double>(runtime.allocation_count) : 0.0;

            result_.reports.resource_summary_rows.push_back(ResourceSummaryRow{
                runtime.resource_id,
                runtime.resource_name,
                runtime.capacity,
                busy_time,
                idle_time,
                utilization,
                runtime.max_queue_length,
                average_wait_time,
                runtime.allocation_count,
                horizon_s,
            });
        }
    }

private:
    void initialize_resources()
    {
        for (const auto& [resource_id, definition] : model_.resources)
        {
            resource_ids_.push_back(resource_id);
        }
        std::sort(resource_ids_.begin(), resource_ids_.end());

        for (const auto& resource_id : resource_ids_)
        {
            const auto& definition = model_.resource(resource_id);
            const auto entity = registry_.create();
            registry_.emplace<ResourceRuntime>(entity, ResourceRuntime{definition.id, definition.name, definition.capacity, 0, 0.0, 0.0, 0, 0.0, 0});
            resource_entities_.insert_or_assign(resource_id, entity);
        }
    }

    void refresh_all_queue_lengths()
    {
        for (const auto& resource_id : resource_ids_)
        {
            auto& runtime = resource_runtime(resource_id);
            runtime.max_queue_length = std::max(runtime.max_queue_length, queue_length_for_resource(resource_id));
        }
    }

    ResourceRuntime& resource_runtime(const std::string& resource_id)
    {
        return registry_.get<ResourceRuntime>(resource_entities_.at(resource_id));
    }

    const ResourceRuntime& resource_runtime(const std::string& resource_id) const
    {
        return registry_.get<ResourceRuntime>(resource_entities_.at(resource_id));
    }

    void update_busy_time(ResourceRuntime& runtime, double time)
    {
        const auto delta = time - runtime.last_update_time;
        if (delta > 0.0)
        {
            runtime.busy_unit_time += static_cast<double>(runtime.in_use) * delta;
        }
        runtime.last_update_time = time;
    }

    entt::registry registry_;
    const SimulationModel& model_;
    SimulationResult& result_;
    DistributionSampler sampler_;
    std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, ScheduledEventCompare> queue_;
    std::deque<PendingTaskRequest> pending_requests_;
    std::unordered_map<std::string, JoinBarrierState> join_barriers_;
    std::unordered_map<std::string, entt::entity> resource_entities_;
    std::vector<std::string> resource_ids_;
    double current_time_{0.0};
    std::uint64_t next_order_{0};
    std::size_t business_sequence_{0};
};

} // namespace

SimulationResult SimulationEngine::run(const SimulationModel& model, const SimulationOptions& options)
{
    SimulationResult result;
    RuntimeContext context(model, result, options.seed);

    auto schedule_outgoing = [&](entt::entity token, const std::string& node_id, double time)
    {
        const auto found = model.outgoing.find(node_id);
        if (found == model.outgoing.end())
        {
            return;
        }
        for (const auto& target_id : found->second)
        {
            context.schedule(ScheduledEvent{time, context.next_order(), ScheduledEventType::ArriveNode, target_id, token});
        }
    };

    auto handle_parallel_gateway = [&](const ScheduledEvent& event)
    {
        if (!context.token_valid(event.token))
        {
            return;
        }

        const auto& node = model.node(event.node_id);
        const auto token_component = context.token(event.token);
        const auto incoming_count = model.incoming.contains(node.id) ? model.incoming.at(node.id).size() : 0U;
        const auto outgoing_count = model.outgoing.contains(node.id) ? model.outgoing.at(node.id).size() : 0U;

        auto continue_from_token = [&](entt::entity token_entity)
        {
            if (outgoing_count == 0)
            {
                return;
            }

            if (outgoing_count == 1)
            {
                context.schedule(ScheduledEvent{event.time, context.next_order(), ScheduledEventType::ArriveNode, model.outgoing.at(node.id).front(), token_entity});
                return;
            }

            const auto barrier_token = context.token(token_entity);
            context.log_event(
                event.time,
                barrier_token,
                node,
                "gateway_fork",
                "{}",
                "{\"branch_count\":" + std::to_string(outgoing_count) + "}");

            int branch_index = 0;
            for (const auto& target_id : model.outgoing.at(node.id))
            {
                const auto child_token_id = barrier_token.token_id + ".p" + std::to_string(branch_index++);
                const auto child_token = context.create_token(barrier_token.entity_id, barrier_token.entity_type, child_token_id, barrier_token.created_at);
                context.schedule(ScheduledEvent{event.time, context.next_order(), ScheduledEventType::ArriveNode, target_id, child_token});
            }
            context.destroy_token(token_entity);
        };

        if (incoming_count > 1)
        {
            auto& barrier = context.join_barriers()[join_key(token_component.entity_id, node.id)];
            barrier.waiting_tokens.push_back(event.token);
            if (barrier.waiting_tokens.size() < incoming_count)
            {
                context.log_event(
                    event.time,
                    token_component,
                    node,
                    "gateway_join_wait",
                    "{}",
                    "{\"arrived\":" + std::to_string(barrier.waiting_tokens.size()) + ",\"required\":" + std::to_string(incoming_count) + "}");
                return;
            }

            const auto merged_token = context.create_token(token_component.entity_id, token_component.entity_type, token_component.token_id + ".joined", token_component.created_at);
            for (const auto waiting_token : barrier.waiting_tokens)
            {
                context.destroy_token(waiting_token);
            }
            barrier.waiting_tokens.clear();
            context.join_barriers().erase(join_key(token_component.entity_id, node.id));

            context.log_event(
                event.time,
                context.token(merged_token),
                node,
                "gateway_join_complete",
                "{}",
                "{\"merged_branches\":" + std::to_string(incoming_count) + "}");

            continue_from_token(merged_token);
            return;
        }

        continue_from_token(event.token);
    };

    auto handle_arrival = [&](const ScheduledEvent& event)
    {
        if (!context.token_valid(event.token))
        {
            return;
        }

        const auto& node = model.node(event.node_id);
        const auto token_component = context.token(event.token);

        if (node.type == NodeType::Task)
        {
            const auto requested_resources = context.task_resources(node.id);
            context.log_event(
                event.time,
                token_component,
                node,
                "task_arrive",
                context.resource_snapshot(node.id, {}),
                "{\"requested_resources\":" + json_array(context.resource_names(requested_resources)) + ",\"resource_strategy\":" + json_string(to_string(node.task->resource_strategy)) + "}");

            const auto allocation = context.allocate_resources_if_possible(node.id, node.task->resource_strategy);
            if (!requested_resources.empty() && allocation.empty())
            {
                context.enqueue_request(PendingTaskRequest{context.next_order(), event.token, node.id, event.time});
                context.log_event(
                    event.time,
                    token_component,
                    node,
                    "task_waiting_for_resources",
                    context.resource_snapshot(node.id, {}),
                    "{\"pending_requests\":" + std::to_string(context.pending_request_count()) + "}");
                return;
            }

            context.start_task(event.token, node, event.time, allocation, 0.0);
            return;
        }

        if (node.type == NodeType::EndEvent)
        {
            context.log_event(
                event.time,
                token_component,
                node,
                "entity_exit",
                "{}",
                "{\"lifetime\":" + format_time(event.time - token_component.created_at) + "}");
            ++result.completed_entities;
            context.destroy_token(event.token);
            return;
        }

        if (node.type == NodeType::ExclusiveGateway)
        {
            const auto& selected_target = model.outgoing.at(node.id).front();
            context.log_event(
                event.time,
                token_component,
                node,
                "gateway_route",
                "{}",
                "{\"selected_target_id\":" + json_string(selected_target) + ",\"routing_mode\":\"first_outgoing\"}");
            context.schedule(ScheduledEvent{event.time, context.next_order(), ScheduledEventType::ArriveNode, selected_target, event.token});
            return;
        }

        if (node.type == NodeType::ParallelGateway)
        {
            handle_parallel_gateway(event);
        }
    };

    auto handle_finish = [&](const ScheduledEvent& event)
    {
        if (!context.token_valid(event.token) || !context.registry().all_of<ActiveTask>(event.token))
        {
            return;
        }

        const auto& node = model.node(event.node_id);
        const auto token_component = context.token(event.token);
        const auto active_task = context.registry().get<ActiveTask>(event.token);
        context.apply_release(active_task.allocated_resources, event.time, token_component.entity_id, node.id);
        context.log_event(
            event.time,
            token_component,
            node,
            "task_finish",
            context.resource_snapshot(node.id, active_task.allocated_resources),
            "{\"processing_time\":" + format_time(event.time - active_task.start_time) + ",\"allocated_resources\":" + json_array(context.resource_names(active_task.allocated_resources)) + "}");
        context.registry().remove<ActiveTask>(event.token);
        schedule_outgoing(event.token, node.id, event.time);
        context.schedule(ScheduledEvent{event.time, context.next_order(), ScheduledEventType::ReevaluatePending, {}, entt::null});
    };

    for (const auto& start_id : model.start_node_ids)
    {
        const auto& start_node = model.node(start_id);
        double next_time = 0.0;
        for (std::size_t index = 0; index < start_node.generator->entity_count; ++index)
        {
            context.schedule(ScheduledEvent{next_time, context.next_order(), ScheduledEventType::GenerateEntity, start_id, entt::null});
            if (index + 1 < start_node.generator->entity_count)
            {
                next_time += context.sample_positive(start_node.generator->interval_distribution);
            }
        }
    }

    while (context.has_events())
    {
        const auto event = context.next_event();
        switch (event.type)
        {
            case ScheduledEventType::GenerateEntity:
            {
                const auto& start_node = model.node(event.node_id);
                const auto entity_id = context.next_entity_id(start_node.name, start_node.generator->entity_type);
                const auto token = context.create_token(entity_id, start_node.generator->entity_type, entity_id + ".t0", event.time);
                const auto token_component = context.token(token);
                ++result.generated_entities;
                context.log_event(
                    event.time,
                    token_component,
                    start_node,
                    "entity_generated",
                    "{}",
                    "{\"generator\":" + json_string(start_node.name) + ",\"entity_type\":" + json_string(start_node.generator->entity_type) + "}");
                schedule_outgoing(token, start_node.id, event.time);
                break;
            }
            case ScheduledEventType::ArriveNode:
                handle_arrival(event);
                break;
            case ScheduledEventType::FinishTask:
                handle_finish(event);
                break;
            case ScheduledEventType::ReevaluatePending:
                context.reevaluate_pending(event.time);
                break;
        }
    }

    context.finalize_resources(result.simulation_horizon);
    return result;
}

} // namespace flux