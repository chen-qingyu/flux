#include "engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
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

struct HeldResources
{
    std::vector<std::string> resource_ids;
};

struct CombineHistory;

struct RestorableTokenSnapshot
{
    ProcessToken token;
    std::shared_ptr<CombineHistory> history;
};

struct CombinedFrame
{
    std::vector<RestorableTokenSnapshot> members;
};

struct CombineHistory
{
    std::vector<CombinedFrame> frames;
};

struct CombineBatch
{
    std::vector<entt::entity> members;
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

struct ScheduledEvent
{
    double time{0.0};
    std::uint64_t order{0};
    ScheduledEventType type{ScheduledEventType::GenerateEntity};
    std::string node_id;
    entt::entity token{entt::null};
};

class RunState
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

    [[nodiscard]] bool token_has_held_resources(entt::entity token_entity) const
    {
        return registry_.all_of<HeldResources>(token_entity) && !registry_.get<HeldResources>(token_entity).resource_ids.empty();
    }

    [[nodiscard]] bool token_has_combine_history(entt::entity token_entity) const
    {
        return registry_.all_of<CombineHistory>(token_entity) && !registry_.get<CombineHistory>(token_entity).frames.empty();
    }

    [[nodiscard]] const std::vector<std::string>& held_resources(entt::entity token_entity) const
    {
        if (registry_.all_of<HeldResources>(token_entity))
        {
            return registry_.get<HeldResources>(token_entity).resource_ids;
        }

        static const std::vector<std::string> empty;
        return empty;
    }

    [[nodiscard]] const ProcessToken& token(entt::entity entity) const
    {
        return registry_.get<ProcessToken>(entity);
    }

    ProcessToken& token(entt::entity entity)
    {
        return registry_.get<ProcessToken>(entity);
    }

    [[nodiscard]] const CombineHistory& combine_history(entt::entity token_entity) const
    {
        return registry_.get<CombineHistory>(token_entity);
    }

    [[nodiscard]] std::shared_ptr<CombineHistory> snapshot_combine_history(entt::entity token_entity) const
    {
        if (!registry_.all_of<CombineHistory>(token_entity))
        {
            return nullptr;
        }

        return std::make_shared<CombineHistory>(registry_.get<CombineHistory>(token_entity));
    }

    [[nodiscard]] RestorableTokenSnapshot snapshot_token(entt::entity token_entity) const
    {
        return RestorableTokenSnapshot{token(token_entity), snapshot_combine_history(token_entity)};
    }

    void copy_combine_history(entt::entity source_token, entt::entity target_token)
    {
        if (!registry_.all_of<CombineHistory>(source_token))
        {
            if (registry_.all_of<CombineHistory>(target_token))
            {
                registry_.remove<CombineHistory>(target_token);
            }
            return;
        }

        registry_.emplace_or_replace<CombineHistory>(target_token, registry_.get<CombineHistory>(source_token));
    }

    void restore_snapshot_history(entt::entity token_entity, const std::shared_ptr<CombineHistory>& history)
    {
        if (!history)
        {
            if (registry_.all_of<CombineHistory>(token_entity))
            {
                registry_.remove<CombineHistory>(token_entity);
            }
            return;
        }

        registry_.emplace_or_replace<CombineHistory>(token_entity, *history);
    }

    [[nodiscard]] entt::entity create_restored_token(const RestorableTokenSnapshot& snapshot)
    {
        const auto restored = create_token(
            snapshot.token.entity_id,
            snapshot.token.entity_type,
            snapshot.token.token_id,
            snapshot.token.created_at);
        restore_snapshot_history(restored, snapshot.history);
        return restored;
    }

    void set_combine_history(entt::entity token_entity, std::vector<RestorableTokenSnapshot> members)
    {
        registry_.emplace_or_replace<CombineHistory>(token_entity, CombineHistory{{CombinedFrame{std::move(members)}}});
    }

    void schedule_token_to_outgoing(const std::string& node_id, entt::entity token_entity, double time)
    {
        const auto found = model_.outgoing.find(node_id);
        if (found == model_.outgoing.end())
        {
            return;
        }

        for (const auto& target_id : found->second)
        {
            schedule(ScheduledEvent{time, next_order(), ScheduledEventType::ArriveNode, target_id, token_entity});
        }
    }

    void schedule_split_outputs(entt::entity token_entity, const NodeDefinition& node, double start_time, double duration)
    {
        if (node.task->type != TaskType::Split)
        {
            return;
        }

        std::vector<entt::entity> outputs;
        if (node.task->split->method == SplitMethod::Ratio)
        {
            outputs.reserve(node.task->split->ratio);
            for (std::size_t index = 0; index < node.task->split->ratio; ++index)
            {
                const auto entity_id = next_entity_id(node.name, node.task->split->entity_type);
                const auto child = create_token(entity_id, node.task->split->entity_type, entity_id + ".t0", start_time);
                copy_combine_history(token_entity, child);
                outputs.push_back(child);
            }
        }
        else
        {
            if (!token_has_combine_history(token_entity))
            {
                throw std::runtime_error("Task '" + node.id + "' requires a previously combined entity when '_method=restore'.");
            }

            const auto& history = combine_history(token_entity);
            const auto& frame = history.frames.back();
            outputs.reserve(frame.members.size());
            for (const auto& snapshot : frame.members)
            {
                outputs.push_back(create_restored_token(snapshot));
            }
        }

        if (outputs.empty())
        {
            return;
        }

        if (node.task->split->one_off)
        {
            for (const auto child : outputs)
            {
                schedule_token_to_outgoing(node.id, child, start_time + duration);
            }
            return;
        }

        const auto interval = duration / static_cast<double>(outputs.size());
        for (std::size_t index = 0; index < outputs.size(); ++index)
        {
            schedule_token_to_outgoing(node.id, outputs[index], start_time + interval * static_cast<double>(index + 1));
        }
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

    void add_transport_distance(double distance)
    {
        result_.total_transport_distance += distance;
    }

    [[nodiscard]] entt::registry& registry()
    {
        return registry_;
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
            // 单资源场景允许省略策略，多资源场景必须在解析阶段显式声明。
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

        // 同一时间点的事件先全部消费，再统一尝试唤醒等待任务，保证“同一时刻按最老可行请求优先”。
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
        if (node.task->type != TaskType::ReleaseResource)
        {
            apply_allocation(allocation, time, wait_time, token_component.entity_id, node.id);
        }
        registry_.emplace_or_replace<ActiveTask>(token_entity, ActiveTask{node.id, allocation});

        const auto duration = sample(node.task->duration_distribution);
        log_event(time, token_component, node, "task_start");

        schedule(ScheduledEvent{time + duration, next_order(), ScheduledEventType::FinishTask, node.id, token_entity});
        if (node.task->type == TaskType::Split)
        {
            schedule_split_outputs(token_entity, node, time, duration);
        }
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

    void add_held_resources(entt::entity token_entity, const std::vector<std::string>& resource_ids)
    {
        if (resource_ids.empty())
        {
            return;
        }

        auto& held = registry_.get_or_emplace<HeldResources>(token_entity);
        held.resource_ids.insert(held.resource_ids.end(), resource_ids.begin(), resource_ids.end());
        // 保持排序，后续 release 直接用二分判断绑定资源是否被持有。
        std::sort(held.resource_ids.begin(), held.resource_ids.end());
    }

    [[nodiscard]] std::vector<std::string> release_resources_for_task(entt::entity token_entity, const std::string& task_id) const
    {
        const auto& currently_held = held_resources(token_entity);
        if (currently_held.empty())
        {
            return {};
        }

        const auto& bound_resources = task_resources(task_id);
        if (bound_resources.empty())
        {
            return currently_held;
        }

        std::vector<std::string> released;
        released.reserve(currently_held.size());
        for (const auto& resource_id : currently_held)
        {
            if (std::binary_search(bound_resources.begin(), bound_resources.end(), resource_id))
            {
                released.push_back(resource_id);
            }
        }
        return released;
    }

    void remove_held_resources(entt::entity token_entity, const std::vector<std::string>& resource_ids)
    {
        if (resource_ids.empty() || !registry_.all_of<HeldResources>(token_entity))
        {
            return;
        }

        auto& held = registry_.get<HeldResources>(token_entity).resource_ids;
        for (const auto& resource_id : resource_ids)
        {
            const auto found = std::find(held.begin(), held.end(), resource_id);
            if (found != held.end())
            {
                held.erase(found);
            }
        }

        if (held.empty())
        {
            registry_.remove<HeldResources>(token_entity);
        }
    }

    void enqueue_combine_member(const std::string& task_id, entt::entity token_entity)
    {
        combine_waiting_[task_id].push_back(token_entity);
    }

    [[nodiscard]] std::vector<entt::entity> take_ready_combine_batch(const std::string& task_id, std::size_t ratio)
    {
        auto found = combine_waiting_.find(task_id);
        if (found == combine_waiting_.end())
        {
            return {};
        }

        auto& waiting = found->second;
        while (!waiting.empty() && !token_valid(waiting.front()))
        {
            waiting.pop_front();
        }
        if (waiting.size() < ratio)
        {
            return {};
        }

        std::vector<entt::entity> members;
        members.reserve(ratio);
        for (std::size_t index = 0; index < ratio; ++index)
        {
            members.push_back(waiting.front());
            waiting.pop_front();
        }
        if (waiting.empty())
        {
            combine_waiting_.erase(found);
        }
        return members;
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
                // 多资源任务需要在任一相关资源释放时重新入候选堆。
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
            // 单资源等待队列直接挂在资源上，多资源等待队列挂在任务自身上。
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

            // 候选堆允许旧条目残留，通过惰性清理避免在 release 路径上全量扫描。
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
    std::unordered_map<std::string, std::deque<entt::entity>> combine_waiting_;
    std::unordered_map<std::string, entt::entity> resource_entities_;
    std::unordered_map<std::string, int> resource_queue_lengths_;
    std::vector<std::string> resource_ids_;
    double current_time_{0.0};
    std::uint64_t next_order_{0};
    std::size_t business_sequence_{0};
    bool pending_resolution_needed_{false};
};

void schedule_start_events(RunState& state);
void process_event(RunState& state, const ScheduledEvent& event);
void handle_generate_entity(RunState& state, const ScheduledEvent& event);
void handle_arrive_node(RunState& state, const ScheduledEvent& event);
void handle_finish_task(RunState& state, const ScheduledEvent& event);
std::string select_exclusive_gateway_target(RunState& state, const NodeDefinition& node);

void schedule_start_events(RunState& state)
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

void process_event(RunState& state, const ScheduledEvent& event)
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

void handle_generate_entity(RunState& state, const ScheduledEvent& event)
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

void handle_arrive_node(RunState& state, const ScheduledEvent& event)
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

        if ((node.task->type == TaskType::Combine || node.task->type == TaskType::Split) && state.token_has_held_resources(event.token))
        {
            throw std::runtime_error("Task '" + node.id + "' does not support tokens that are holding resources.");
        }

        if (node.task->type == TaskType::Combine)
        {
            state.enqueue_combine_member(node.id, event.token);
            const auto members = state.take_ready_combine_batch(node.id, node.task->combine->ratio);
            if (members.empty())
            {
                return;
            }

            std::vector<RestorableTokenSnapshot> snapshots;
            snapshots.reserve(members.size());
            for (const auto member : members)
            {
                snapshots.push_back(state.snapshot_token(member));
            }

            const auto entity_id = state.next_entity_id(node.name, node.task->combine->entity_type);
            const auto batch_token = state.create_token(entity_id, node.task->combine->entity_type, entity_id + ".t0", event.time);
            state.registry().emplace<CombineBatch>(batch_token, CombineBatch{members});
            state.set_combine_history(batch_token, std::move(snapshots));

            if (requested_resources.empty())
            {
                state.start_task(batch_token, node, event.time, {}, 0.0);
                return;
            }

            if (!state.has_pending_requests())
            {
                const auto allocation = state.allocate_resources_if_possible(node.id, node.task->resource_strategy);
                if (!allocation.empty())
                {
                    state.start_task(batch_token, node, event.time, allocation, 0.0);
                    return;
                }
            }

            state.enqueue_request(PendingTaskRequest{state.next_order(), batch_token, node.id, event.time});
            state.log_event(event.time, state.token(batch_token), node, "task_waiting_for_resources");
            return;
        }

        if (node.task->type == TaskType::ReleaseResource)
        {
            state.start_task(event.token, node, event.time, state.release_resources_for_task(event.token, node.id), 0.0);
            return;
        }

        if (requested_resources.empty())
        {
            state.start_task(event.token, node, event.time, {}, 0.0);
            return;
        }

        if (!state.has_pending_requests())
        {
            // 没有历史等待者时允许直接抢占；一旦存在等待队列，必须走统一仲裁路径。
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
}

void handle_finish_task(RunState& state, const ScheduledEvent& event)
{
    if (!state.token_valid(event.token) || !state.registry().all_of<ActiveTask>(event.token))
    {
        return;
    }

    const auto& node = flux::node(state.model(), event.node_id);
    const auto token_component = state.token(event.token);
    const auto active_task = state.registry().get<ActiveTask>(event.token);
    if (node.task->type == TaskType::Delay || node.task->type == TaskType::Transport)
    {
        state.apply_release(active_task.allocated_resources, event.time, token_component.entity_id, node.id);
    }
    else if (node.task->type == TaskType::Combine)
    {
        state.apply_release(active_task.allocated_resources, event.time, token_component.entity_id, node.id);
        if (state.registry().all_of<CombineBatch>(event.token))
        {
            const auto members = state.registry().get<CombineBatch>(event.token).members;
            for (const auto member : members)
            {
                state.destroy_token(member);
            }
            state.registry().remove<CombineBatch>(event.token);
        }
    }
    else if (node.task->type == TaskType::AcquireResource)
    {
        state.add_held_resources(event.token, active_task.allocated_resources);
    }
    else if (node.task->type == TaskType::ReleaseResource)
    {
        state.apply_release(active_task.allocated_resources, event.time, token_component.entity_id, node.id);
        state.remove_held_resources(event.token, active_task.allocated_resources);
    }
    else if (node.task->type == TaskType::Split)
    {
        state.apply_release(active_task.allocated_resources, event.time, token_component.entity_id, node.id);
    }
    if (node.task->type == TaskType::Transport)
    {
        state.add_transport_distance(node.task->distance);
    }
    state.log_event(event.time, token_component, node, "task_finish");
    state.registry().remove<ActiveTask>(event.token);

    if (node.task->type == TaskType::Split)
    {
        state.destroy_token(event.token);
        return;
    }

    state.schedule_token_to_outgoing(node.id, event.token, event.time);
}

std::string select_exclusive_gateway_target(RunState& state, const NodeDefinition& node)
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

} // namespace

Result Engine::run(const Model& model, std::uint64_t seed)
{
    Result result;
    RunState state(model, result, seed);

    schedule_start_events(state);

    while (state.has_events())
    {
        const auto batch_time = state.next_event_time();
        // 事件队列按 (time, order) 排序，这里按时间批处理来稳定同一时刻的资源仲裁结果。
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

} // namespace flux