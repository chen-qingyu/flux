#include "engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
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

enum class PendingQueueScope
{
    Resource,
    Task,
};

struct PendingQueueKey
{
    PendingQueueScope scope{PendingQueueScope::Resource};
    std::string id;

    bool operator==(const PendingQueueKey&) const = default;
};

struct PendingQueueKeyHash
{
    std::size_t operator()(const PendingQueueKey& key) const
    {
        auto hash = std::hash<std::string>{}(key.id);
        hash ^= static_cast<std::size_t>(key.scope) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

struct PendingCandidate
{
    std::uint64_t order{0};
    PendingQueueKey key;
};

struct PendingCandidateCompare
{
    bool operator()(const PendingCandidate& left, const PendingCandidate& right) const
    {
        return left.order > right.order;
    }
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

    double sample(const DistributionSpec& spec)
    {
        switch (spec.type)
        {
            case DistributionType::Static:
                return spec.first;
            case DistributionType::Uniform:
                return sample_uniform(spec.first, spec.second);
            case DistributionType::Exponential:
            {
                std::exponential_distribution<double> distribution(1.0 / spec.first);
                return distribution(generator_);
            }
            case DistributionType::Normal:
            {
                std::normal_distribution<double> distribution(spec.first, spec.second);
                const auto value = distribution(generator_);
                return value >= 0.0 ? value : 0.0;
            }
            case DistributionType::LogNormal:
            {
                std::lognormal_distribution<double> distribution(spec.first, spec.second);
                return distribution(generator_);
            }
        }

        return 0.0;
    }

    double sample_uniform(double minimum, double maximum)
    {
        std::uniform_real_distribution<double> distribution(minimum, maximum);
        return distribution(generator_);
    }

private:
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
        initialize_task_queue_index();
    }

    void schedule(ScheduledEvent event)
    {
        queue_.push(std::move(event));
    }

    [[nodiscard]] bool has_events() const
    {
        return !queue_.empty();
    }

    [[nodiscard]] double next_event_time() const
    {
        return queue_.top().time;
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

    double sample(const DistributionSpec& spec)
    {
        return sampler_.sample(spec);
    }

    double sample_uniform(double minimum, double maximum)
    {
        return sampler_.sample_uniform(minimum, maximum);
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

    [[nodiscard]] bool has_pending_requests() const
    {
        return !pending_requests_.empty();
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

    [[nodiscard]] std::vector<std::string> allocate_resources_if_possible(const std::string& task_id, const std::optional<ResourceStrategy>& strategy)
    {
        const auto& resource_ids = task_resources(task_id);
        if (resource_ids.empty())
        {
            return {};
        }

        if (!strategy.has_value())
        {
            if (resource_ids.size() != 1)
            {
                return {};
            }

            const auto& resource_id = resource_ids.front();
            const auto& runtime = resource_runtime(resource_id);
            if (runtime.in_use >= runtime.capacity)
            {
                return {};
            }
            return {resource_id};
        }

        if (*strategy == ResourceStrategy::All)
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

        if (*strategy == ResourceStrategy::Any)
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

    struct PendingCandidateView
    {
        PendingQueueKey key;
        std::vector<std::string> allocation;
    };

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

            rearm_resource_queues(resource_id);
        }

        if (has_pending_requests())
        {
            pending_resolution_needed_ = true;
        }
    }

    void enqueue_request(PendingTaskRequest request)
    {
        note_request_enqueued(request);
        pending_resolution_needed_ = true;

        const auto key = pending_queue_key_for_task(request.task_id);
        auto& requests = pending_requests_[key];
        const auto was_empty = requests.empty();
        requests.push_back(std::move(request));
        if (was_empty)
        {
            push_pending_candidate_if_waiting(key);
        }
    }

    void resolve_pending(double time)
    {
        if (!pending_resolution_needed_)
        {
            return;
        }

        pending_resolution_needed_ = false;

        while (true)
        {
            auto candidate = next_pending_candidate();
            if (!candidate.has_value())
            {
                break;
            }

            start_pending_request(std::move(*candidate), time);
        }
    }

    void start_task(entt::entity token_entity, const NodeDefinition& node, double time, const std::vector<std::string>& allocation, double wait_time)
    {
        auto& token_component = token(token_entity);
        apply_allocation(allocation, time, wait_time, token_component.entity_id, node.id);
        registry_.emplace_or_replace<ActiveTask>(token_entity, ActiveTask{node.id, allocation});

        const auto duration = sample(node.task->duration_distribution);
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
    using PendingRequestMap = std::unordered_map<PendingQueueKey, std::deque<PendingTaskRequest>, PendingQueueKeyHash>;

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

    void initialize_task_queue_index()
    {
        for (const auto& [task_id, resource_ids] : model_.task_resources)
        {
            if (resource_ids.size() <= 1)
            {
                continue;
            }

            for (const auto& resource_id : resource_ids)
            {
                task_queue_ids_by_resource_[resource_id].push_back(task_id);
            }
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

    [[nodiscard]] static PendingQueueKey resource_queue_key(const std::string& resource_id)
    {
        return PendingQueueKey{PendingQueueScope::Resource, resource_id};
    }

    [[nodiscard]] static PendingQueueKey task_queue_key(const std::string& task_id)
    {
        return PendingQueueKey{PendingQueueScope::Task, task_id};
    }

    [[nodiscard]] PendingQueueKey pending_queue_key_for_task(const std::string& task_id) const
    {
        const auto& resource_ids = task_resources(task_id);
        if (resource_ids.size() == 1)
        {
            return resource_queue_key(resource_ids.front());
        }

        return task_queue_key(task_id);
    }

    void rearm_resource_queues(const std::string& resource_id)
    {
        push_pending_candidate_if_waiting(resource_queue_key(resource_id));

        const auto found = task_queue_ids_by_resource_.find(resource_id);
        if (found == task_queue_ids_by_resource_.end())
        {
            return;
        }

        for (const auto& task_id : found->second)
        {
            push_pending_candidate_if_waiting(task_queue_key(task_id));
        }
    }

    [[nodiscard]] std::optional<PendingCandidateView> next_pending_candidate()
    {
        while (!pending_candidates_.empty())
        {
            const auto candidate = pending_candidates_.top();

            discard_invalid_fronts(candidate.key);

            const auto found = pending_requests_.find(candidate.key);
            if (found == pending_requests_.end())
            {
                pending_candidates_.pop();
                continue;
            }

            const auto& request = found->second.front();
            if (request.order != candidate.order)
            {
                pending_candidates_.pop();
                continue;
            }

            const auto& node = flux::node(model_, request.task_id);
            auto allocation = allocate_resources_if_possible(request.task_id, node.task->resource_strategy);
            if (allocation.empty())
            {
                pending_candidates_.pop();
                continue;
            }

            return PendingCandidateView{candidate.key, std::move(allocation)};
        }

        return std::nullopt;
    }

    void push_pending_candidate(const PendingQueueKey& key, std::uint64_t order)
    {
        pending_candidates_.push(PendingCandidate{order, key});
    }

    void push_pending_candidate_if_waiting(const PendingQueueKey& key)
    {
        const auto found = pending_requests_.find(key);
        if (found == pending_requests_.end() || found->second.empty())
        {
            return;
        }

        push_pending_candidate(key, found->second.front().order);
    }

    void discard_invalid_fronts(const PendingQueueKey& key)
    {
        const auto found = pending_requests_.find(key);
        if (found == pending_requests_.end())
        {
            return;
        }

        auto& requests = found->second;
        auto removed_any = false;
        while (!requests.empty() && !token_valid(requests.front().token))
        {
            note_request_dequeued(requests.front());
            requests.pop_front();
            removed_any = true;
        }

        if (requests.empty())
        {
            pending_requests_.erase(found);
            return;
        }

        if (removed_any)
        {
            push_pending_candidate(key, requests.front().order);
        }
    }

    PendingTaskRequest take_front_request(const PendingQueueKey& key)
    {
        auto& requests = pending_requests_.at(key);
        auto request = std::move(requests.front());
        requests.pop_front();
        pending_candidates_.pop();

        if (requests.empty())
        {
            pending_requests_.erase(key);
        }
        else
        {
            push_pending_candidate(key, requests.front().order);
        }

        return request;
    }

    void start_pending_request(PendingCandidateView candidate, double time)
    {
        auto request = take_front_request(candidate.key);

        const auto& node = flux::node(model_, request.task_id);
        note_request_dequeued(request);
        start_task(request.token, node, time, candidate.allocation, time - request.arrival_time);
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
    PendingRequestMap pending_requests_;
    std::priority_queue<PendingCandidate, std::vector<PendingCandidate>, PendingCandidateCompare> pending_candidates_;
    std::unordered_map<std::string, std::vector<std::string>> task_queue_ids_by_resource_;
    std::unordered_map<std::string, JoinBarrierState> join_barriers_;
    std::unordered_map<std::string, entt::entity> resource_entities_;
    std::unordered_map<std::string, int> resource_queue_lengths_;
    std::vector<std::string> resource_ids_;
    double current_time_{0.0};
    std::uint64_t next_order_{0};
    std::size_t business_sequence_{0};
    bool pending_resolution_needed_{false};
};

Result Engine::run(const Model& model, std::uint64_t seed) const
{
    Result result;
    RunState state(model, result, seed);

    schedule_start_events(state);

    while (state.has_events())
    {
        const auto batch_time = state.next_event_time();
        do
        {
            const auto event = state.next_event();
            process_event(state, event);
        } while (state.has_events() && state.next_event_time() == batch_time);

        state.resolve_pending(batch_time);
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
                next_time += state.sample(start_node.generator->interval_distribution);
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

        if (!state.has_pending_requests())
        {
            const auto allocation = state.allocate_resources_if_possible(node.id, node.task->resource_strategy);
            if (!allocation.empty())
            {
                state.start_task(event.token, node, event.time, allocation, 0.0);
                return;
            }
        }

        state.enqueue_request(PendingTaskRequest{state.next_order(), event.token, node.id, event.time});
        state.log_event(event.time, token_component, node, "task_waiting_for_resources");
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
        const auto selected_target = select_exclusive_gateway_target(state, node);
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
}

std::string Engine::select_exclusive_gateway_target(RunState& state, const NodeDefinition& node) const
{
    if (!node.gateway_criteria.has_value())
    {
        throw std::runtime_error("Exclusive gateway '" + node.id + "' must define routing criteria before execution.");
    }

    if (*node.gateway_criteria != GatewayCriteria::ByWeight)
    {
        throw std::runtime_error("Unsupported exclusive gateway routing criteria.");
    }

    const auto& flow_ids = state.model().outgoing_flow_ids.at(node.id);
    if (flow_ids.size() == 1)
    {
        return flux::flow(state.model(), flow_ids.front()).target_id;
    }

    double total_weight = 0.0;
    for (const auto& flow_id : flow_ids)
    {
        total_weight += flux::flow(state.model(), flow_id).weight.value_or(0.0);
    }

    const auto threshold = state.sample_uniform(0.0, total_weight);
    double cumulative_weight = 0.0;
    for (const auto& flow_id : flow_ids)
    {
        const auto& candidate = flux::flow(state.model(), flow_id);
        cumulative_weight += candidate.weight.value_or(0.0);
        if (threshold < cumulative_weight)
        {
            return candidate.target_id;
        }
    }

    return flux::flow(state.model(), flow_ids.back()).target_id;
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