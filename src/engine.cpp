#include "engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <entt/entt.hpp>
#include <magic_enum/magic_enum.hpp>

#include "tools.hpp"

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
    WakeResourceQueue,
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

struct JoinBarrierState
{
    std::vector<entt::entity> waiting_tokens;
};

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

} // namespace

struct Engine::ScheduledEvent
{
    double time{0.0};
    std::uint64_t order{0};
    ScheduledEventType type{ScheduledEventType::GenerateEntity};
    std::string node_id;
    entt::entity token{entt::null};
};

class Engine::RunState
{
public:
    RunState(const Model& model, Result& result, std::uint64_t seed)
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

    void log_event(double time, const ProcessToken& token_component, const NodeDefinition& node, const std::string& event_type)
    {
        result_.reports.event_rows.push_back(EventLogRow{
            time,
            token_component.entity_id,
            token_component.entity_type,
            node.id,
            node.name,
            std::string(magic_enum::enum_name(node.type)),
            event_type,
        });
    }

    void log_resource_timeline(double time, const ResourceRuntime& runtime, const std::string& change_type, int queue_length, const std::string& entity_id, const std::string& task_id)
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
        });
    }

    [[nodiscard]] const Model& model() const
    {
        return model_;
    }

    void increment_generated_entities()
    {
        ++result_.generated_entities;
    }

    void increment_completed_entities()
    {
        ++result_.completed_entities;
    }

    [[nodiscard]] entt::registry& registry()
    {
        return registry_;
    }

    [[nodiscard]] std::unordered_map<std::string, JoinBarrierState>& join_barriers()
    {
        return join_barriers_;
    }

    [[nodiscard]] const std::vector<std::string>& task_resources(const std::string& task_id) const
    {
        if (const auto found = model_.task_resources.find(task_id); found != model_.task_resources.end())
        {
            return found->second;
        }
        static const std::vector<std::string> empty;
        return empty;
    }

    [[nodiscard]] int queue_length_for_resource(const std::string& resource_id) const
    {
        if (const auto found = resource_queue_lengths_.find(resource_id); found != resource_queue_lengths_.end())
        {
            return found->second;
        }
        return 0;
    }

    [[nodiscard]] std::vector<std::string> allocate_resources_if_possible(const std::string& task_id, ResourceStrategy strategy)
    {
        const auto& resource_ids = task_resources(task_id);
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
            const auto queue_length = queue_length_for_resource(resource_id);
            update_busy_time(runtime, time);
            ++runtime.in_use;
            ++runtime.allocation_count;
            runtime.total_wait_time += wait_time;
            runtime.max_queue_length = std::max(runtime.max_queue_length, queue_length);
            log_resource_timeline(
                time,
                runtime,
                "allocate",
                queue_length,
                entity_id,
                task_id);
        }
    }

    void apply_release(const std::vector<std::string>& resource_ids, double time, const std::string& entity_id, const std::string& task_id)
    {
        for (const auto& resource_id : resource_ids)
        {
            auto& runtime = resource_runtime(resource_id);
            const auto queue_length = queue_length_for_resource(resource_id);
            update_busy_time(runtime, time);
            runtime.in_use = std::max(0, runtime.in_use - 1);
            runtime.max_queue_length = std::max(runtime.max_queue_length, queue_length);
            log_resource_timeline(
                time,
                runtime,
                "release",
                queue_length,
                entity_id,
                task_id);
        }
    }

    void enqueue_request(PendingTaskRequest request)
    {
        note_request_enqueued(request);

        const auto& resource_ids = task_resources(request.task_id);
        if (resource_ids.size() == 1)
        {
            single_resource_pending_requests_[resource_ids.front()].push_back(std::move(request));
            return;
        }

        pending_requests_.push_back(std::move(request));
    }

    void reevaluate_pending(double time)
    {
        reevaluate_pending_scheduled_ = false;
        if (pending_requests_.empty())
        {
            return;
        }

        std::deque<PendingTaskRequest> remaining_requests;
        while (!pending_requests_.empty())
        {
            auto request = std::move(pending_requests_.front());
            pending_requests_.pop_front();

            if (!token_valid(request.token))
            {
                note_request_dequeued(request);
                continue;
            }

            const auto& node = flux::node(model_, request.task_id);
            const auto& required_resources = task_resources(request.task_id);
            if (required_resources.empty())
            {
                note_request_dequeued(request);
                start_task(request.token, node, time, {}, time - request.arrival_time);
                continue;
            }

            const auto allocation = allocate_resources_if_possible(request.task_id, node.task->resource_strategy.value());
            if (allocation.empty())
            {
                remaining_requests.push_back(std::move(request));
                continue;
            }

            note_request_dequeued(request);
            start_task(request.token, node, time, allocation, time - request.arrival_time);
        }

        pending_requests_ = std::move(remaining_requests);
    }

    void process_single_resource_pending(const std::string& resource_id, double time)
    {
        resource_queue_wake_scheduled_[resource_id] = false;

        auto found = single_resource_pending_requests_.find(resource_id);
        if (found == single_resource_pending_requests_.end())
        {
            return;
        }

        auto& requests = found->second;
        auto& runtime = resource_runtime(resource_id);
        while (runtime.in_use < runtime.capacity && !requests.empty())
        {
            auto request = std::move(requests.front());
            requests.pop_front();

            if (!token_valid(request.token))
            {
                note_request_dequeued(request);
                continue;
            }

            const auto& node = flux::node(model_, request.task_id);
            note_request_dequeued(request);
            start_task(request.token, node, time, {resource_id}, time - request.arrival_time);
        }

        if (requests.empty())
        {
            single_resource_pending_requests_.erase(found);
        }
    }

    void schedule_reevaluate_pending(double time)
    {
        if (reevaluate_pending_scheduled_ || pending_requests_.empty())
        {
            return;
        }

        reevaluate_pending_scheduled_ = true;
        schedule(ScheduledEvent{time, next_order(), ScheduledEventType::ReevaluatePending, {}, entt::null});
    }

    void schedule_resource_queue_wake(const std::string& resource_id, double time)
    {
        const auto found = single_resource_pending_requests_.find(resource_id);
        if (found == single_resource_pending_requests_.end() || found->second.empty())
        {
            return;
        }

        if (resource_queue_wake_scheduled_[resource_id])
        {
            return;
        }

        resource_queue_wake_scheduled_[resource_id] = true;
        schedule(ScheduledEvent{time, next_order(), ScheduledEventType::WakeResourceQueue, resource_id, entt::null});
    }

    void start_task(entt::entity token_entity, const NodeDefinition& node, double time, const std::vector<std::string>& allocation, double wait_time)
    {
        auto& token_component = token(token_entity);
        apply_allocation(allocation, time, wait_time, token_component.entity_id, node.id);
        registry_.emplace_or_replace<ActiveTask>(token_entity, ActiveTask{node.id, time - wait_time, time, allocation});

        const auto duration = sample_positive(node.task->duration_distribution);
        log_event(time, token_component, node, "task_start");

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

    void initialize_resources()
    {
        for (const auto& [resource_id, definition] : model_.resources)
        {
            resource_ids_.push_back(resource_id);
        }
        std::sort(resource_ids_.begin(), resource_ids_.end());

        for (const auto& resource_id : resource_ids_)
        {
            const auto& definition = flux::resource(model_, resource_id);
            const auto entity = registry_.create();
            registry_.emplace<ResourceRuntime>(entity, ResourceRuntime{definition.id, definition.name, definition.capacity, 0, 0.0, 0.0, 0, 0.0, 0});
            resource_entities_.insert_or_assign(resource_id, entity);
            resource_queue_lengths_.insert_or_assign(resource_id, 0);
        }
    }

    void note_request_enqueued(const PendingTaskRequest& request)
    {
        for (const auto& resource_id : task_resources(request.task_id))
        {
            auto& queue_length = resource_queue_lengths_[resource_id];
            ++queue_length;
            auto& runtime = resource_runtime(resource_id);
            runtime.max_queue_length = std::max(runtime.max_queue_length, queue_length);
        }
    }

    void note_request_dequeued(const PendingTaskRequest& request)
    {
        for (const auto& resource_id : task_resources(request.task_id))
        {
            auto& queue_length = resource_queue_lengths_[resource_id];
            queue_length = std::max(0, queue_length - 1);
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
    const Model& model_;
    Result& result_;
    DistributionSampler sampler_;
    std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, ScheduledEventCompare> queue_;
    std::deque<PendingTaskRequest> pending_requests_;
    std::unordered_map<std::string, std::deque<PendingTaskRequest>> single_resource_pending_requests_;
    std::unordered_map<std::string, JoinBarrierState> join_barriers_;
    std::unordered_map<std::string, entt::entity> resource_entities_;
    std::unordered_map<std::string, int> resource_queue_lengths_;
    std::unordered_map<std::string, bool> resource_queue_wake_scheduled_;
    std::vector<std::string> resource_ids_;
    double current_time_{0.0};
    std::uint64_t next_order_{0};
    std::size_t business_sequence_{0};
    bool reevaluate_pending_scheduled_{false};
};

Result Engine::run(const Model& model, const Options& options)
{
    Result result;
    RunState state(model, result, options.seed);

    schedule_start_events(state);

    while (state.has_events())
    {
        const auto event = state.next_event();
        process_event(state, event);
    }

    state.finalize_resources(result.simulation_horizon);
    return result;
}

void Engine::schedule_start_events(RunState& state) const
{
    for (const auto& start_id : state.model().start_node_ids)
    {
        const auto& start_node = flux::node(state.model(), start_id);
        double next_time = 0.0;
        for (std::size_t index = 0; index < start_node.generator->entity_count; ++index)
        {
            state.schedule(ScheduledEvent{next_time, state.next_order(), ScheduledEventType::GenerateEntity, start_id, entt::null});
            if (index + 1 < start_node.generator->entity_count)
            {
                next_time += state.sample_positive(start_node.generator->interval_distribution);
            }
        }
    }
}

void Engine::process_event(RunState& state, const ScheduledEvent& event) const
{
    switch (event.type)
    {
        case ScheduledEventType::GenerateEntity:
            handle_generate_entity(state, event);
            break;
        case ScheduledEventType::ArriveNode:
            handle_arrive_node(state, event);
            break;
        case ScheduledEventType::FinishTask:
            handle_finish_task(state, event);
            break;
        case ScheduledEventType::ReevaluatePending:
            state.reevaluate_pending(event.time);
            break;
        case ScheduledEventType::WakeResourceQueue:
            state.process_single_resource_pending(event.node_id, event.time);
            break;
    }
}

void Engine::handle_generate_entity(RunState& state, const ScheduledEvent& event) const
{
    const auto& start_node = flux::node(state.model(), event.node_id);
    const auto entity_id = state.next_entity_id(start_node.name, start_node.generator->entity_type);
    const auto token = state.create_token(entity_id, start_node.generator->entity_type, entity_id + ".t0", event.time);
    const auto token_component = state.token(token);
    state.increment_generated_entities();
    state.log_event(event.time, token_component, start_node, "entity_generated");

    const auto found = state.model().outgoing.find(start_node.id);
    if (found == state.model().outgoing.end())
    {
        return;
    }
    for (const auto& target_id : found->second)
    {
        state.schedule(ScheduledEvent{event.time, state.next_order(), ScheduledEventType::ArriveNode, target_id, token});
    }
}

void Engine::handle_arrive_node(RunState& state, const ScheduledEvent& event) const
{
    if (!state.token_valid(event.token))
    {
        return;
    }

    const auto& node = flux::node(state.model(), event.node_id);
    const auto token_component = state.token(event.token);

    if (node.type == NodeType::Task)
    {
        const auto& requested_resources = state.task_resources(node.id);
        state.log_event(event.time, token_component, node, "task_arrive");

        if (requested_resources.empty())
        {
            state.start_task(event.token, node, event.time, {}, 0.0);
            return;
        }

        const auto allocation = state.allocate_resources_if_possible(node.id, node.task->resource_strategy.value());
        if (allocation.empty())
        {
            state.enqueue_request(PendingTaskRequest{state.next_order(), event.token, node.id, event.time});
            state.log_event(event.time, token_component, node, "task_waiting_for_resources");
            return;
        }

        state.start_task(event.token, node, event.time, allocation, 0.0);
        return;
    }

    if (node.type == NodeType::EndEvent)
    {
        state.log_event(event.time, token_component, node, "entity_exit");
        state.increment_completed_entities();
        state.destroy_token(event.token);
        return;
    }

    if (node.type == NodeType::ExclusiveGateway)
    {
        const auto& selected_target = state.model().outgoing.at(node.id).front();
        state.log_event(event.time, token_component, node, "gateway_route");
        state.schedule(ScheduledEvent{event.time, state.next_order(), ScheduledEventType::ArriveNode, selected_target, event.token});
        return;
    }

    if (node.type == NodeType::ParallelGateway)
    {
        handle_parallel_gateway(state, event);
    }
}

void Engine::handle_finish_task(RunState& state, const ScheduledEvent& event) const
{
    if (!state.token_valid(event.token) || !state.registry().all_of<ActiveTask>(event.token))
    {
        return;
    }

    const auto& node = flux::node(state.model(), event.node_id);
    const auto token_component = state.token(event.token);
    const auto active_task = state.registry().get<ActiveTask>(event.token);
    state.apply_release(active_task.allocated_resources, event.time, token_component.entity_id, node.id);
    state.log_event(event.time, token_component, node, "task_finish");
    state.registry().remove<ActiveTask>(event.token);

    const auto found = state.model().outgoing.find(node.id);
    if (found != state.model().outgoing.end())
    {
        for (const auto& target_id : found->second)
        {
            state.schedule(ScheduledEvent{event.time, state.next_order(), ScheduledEventType::ArriveNode, target_id, event.token});
        }
    }
    for (const auto& resource_id : active_task.allocated_resources)
    {
        state.schedule_resource_queue_wake(resource_id, event.time);
    }
    state.schedule_reevaluate_pending(event.time);
}

void Engine::handle_parallel_gateway(RunState& state, const ScheduledEvent& event) const
{
    if (!state.token_valid(event.token))
    {
        return;
    }

    const auto& node = flux::node(state.model(), event.node_id);
    const auto token_component = state.token(event.token);
    const auto incoming_count = state.model().incoming.contains(node.id) ? state.model().incoming.at(node.id).size() : 0U;
    const auto outgoing_count = state.model().outgoing.contains(node.id) ? state.model().outgoing.at(node.id).size() : 0U;

    if (incoming_count > 1)
    {
        auto& barrier = state.join_barriers()[join_key(token_component.entity_id, node.id)];
        barrier.waiting_tokens.push_back(event.token);
        if (barrier.waiting_tokens.size() < incoming_count)
        {
            state.log_event(event.time, token_component, node, "gateway_join_wait");
            return;
        }

        const auto merged_token = state.create_token(token_component.entity_id, token_component.entity_type, token_component.token_id + ".joined", token_component.created_at);
        for (const auto waiting_token : barrier.waiting_tokens)
        {
            state.destroy_token(waiting_token);
        }
        barrier.waiting_tokens.clear();
        state.join_barriers().erase(join_key(token_component.entity_id, node.id));

        state.log_event(event.time, state.token(merged_token), node, "gateway_join_complete");
        continue_parallel_gateway(state, event, outgoing_count, merged_token);
        return;
    }

    continue_parallel_gateway(state, event, outgoing_count, event.token);
}

void Engine::continue_parallel_gateway(RunState& state, const ScheduledEvent& event, std::size_t outgoing_count, entt::entity token_entity) const
{
    if (outgoing_count == 0)
    {
        return;
    }

    const auto& node = flux::node(state.model(), event.node_id);
    if (outgoing_count == 1)
    {
        state.schedule(ScheduledEvent{event.time, state.next_order(), ScheduledEventType::ArriveNode, state.model().outgoing.at(node.id).front(), token_entity});
        return;
    }

    const auto barrier_token = state.token(token_entity);
    state.log_event(event.time, barrier_token, node, "gateway_fork");

    int branch_index = 0;
    for (const auto& target_id : state.model().outgoing.at(node.id))
    {
        const auto child_token_id = barrier_token.token_id + ".p" + std::to_string(branch_index++);
        const auto child_token = state.create_token(barrier_token.entity_id, barrier_token.entity_type, child_token_id, barrier_token.created_at);
        state.schedule(ScheduledEvent{event.time, state.next_order(), ScheduledEventType::ArriveNode, target_id, child_token});
    }
    state.destroy_token(token_entity);
}

} // namespace flux