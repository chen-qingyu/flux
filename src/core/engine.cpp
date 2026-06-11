#include "engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
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

enum class ScheduledEventType
{
    GenerateEntity,
    ArriveNode,
    FinishTask,
};

struct ProcessToken
{
    std::size_t global_seq{0};
    std::string entity_type;
    double created_at{0.0};
    std::string entity_name;
    std::unordered_map<std::string, std::string> properties;
};

struct ActiveTask
{
    std::string task_id;
    std::vector<std::string> allocated_resources;
    double start_time{0.0};
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
    std::size_t restore_count{1};
    std::optional<std::string> quantity;
    std::vector<std::string> held_resource_ids;
};

struct CombineHistory
{
    std::vector<RestorableTokenSnapshot> members;
};

struct CombineBatch
{
    struct Member
    {
        entt::entity token{entt::null};
        std::size_t consumed_units{0};
    };

    std::vector<Member> members;
};

struct CombineGroupKey
{
    std::string task_id;
    std::string group;

    bool operator==(const CombineGroupKey&) const = default;
};

struct CombineGroupKeyHash
{
    std::size_t operator()(const CombineGroupKey& key) const
    {
        auto hash = std::hash<std::string>{}(key.task_id);
        hash ^= std::hash<std::string>{}(key.group) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

struct RatioProgress
{
    std::size_t processed_inputs{0};
    std::size_t emitted_outputs{0};
};

struct WaitingCombineToken
{
    entt::entity token{entt::null};
    std::size_t remaining_units{0};
};

struct CombineTokenState
{
    std::size_t waiting_units{0};
    std::size_t in_flight_units{0};
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

struct TaskRuntime
{
    struct SampleStats
    {
        std::size_t count{0};
        double total_time{0.0};
        double max_time{0.0};
        double min_time{std::numeric_limits<double>::infinity()};

        void note_sample(double duration)
        {
            ++count;
            total_time += duration;
            max_time = std::max(max_time, duration);
            min_time = std::min(min_time, duration);
        }

        [[nodiscard]] double average_time() const
        {
            return count > 0 ? total_time / static_cast<double>(count) : 0.0;
        }

        [[nodiscard]] double max_or_zero() const
        {
            return count > 0 ? max_time : 0.0;
        }

        [[nodiscard]] double min_or_zero() const
        {
            return count > 0 ? min_time : 0.0;
        }
    };

    std::string task_id;
    std::string task_name;
    std::size_t entity_count{0};
    double busy_time{0.0};
    double last_busy_start_time{0.0};
    std::size_t active_count{0};
    int waiting_count{0};
    int running_count{0};
    int completed_count{0};
    SampleStats queue_stats;
    SampleStats process_stats;
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

    [[nodiscard]] bool operator<(const PendingCandidate& other) const
    {
        return order > other.order;
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
            default:
                throw std::runtime_error("unreachable");
        }
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
    std::optional<std::size_t> external_record_index;

    [[nodiscard]] bool operator<(const ScheduledEvent& other) const
    {
        // priority_queue 默认取“最大”元素为堆顶，这里反向比较以保持最早事件优先。
        if (time != other.time)
        {
            return time > other.time;
        }
        return order > other.order;
    }
};

class Engine::ResourceManager
{
public:
    explicit ResourceManager(const Model& model)
        : model_(model)
    {
    }

    void initialize(entt::registry& registry)
    {
        for (const auto& [resource_id, definition] : model_.resources)
        {
            resource_ids_.push_back(resource_id);
        }
        std::sort(resource_ids_.begin(), resource_ids_.end());

        for (const auto& resource_id : resource_ids_)
        {
            const auto& definition = flux::resource(model_, resource_id);
            const auto entity = registry.create();
            registry.emplace<ResourceRuntime>(entity, ResourceRuntime{definition.id, definition.name, definition.capacity, 0, 0.0, 0.0, 0, 0.0, 0});
            resource_entities_.insert_or_assign(resource_id, entity);
            resource_queue_lengths_.insert_or_assign(resource_id, 0);
        }
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

    [[nodiscard]] std::vector<std::string> allocate_resources_if_possible(
        entt::registry& registry,
        const std::string& task_id,
        const std::optional<ResourceStrategy>& strategy) const
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
            const auto& runtime = resource_runtime(registry, resource_id);
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
                const auto& runtime = resource_runtime(registry, resource_id);
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
                const auto& runtime = resource_runtime(registry, resource_id);
                if (runtime.in_use < runtime.capacity)
                {
                    return {resource_id};
                }
            }
        }

        return {};
    }

    void apply_allocation(
        entt::registry& registry,
        Result& result,
        const std::vector<std::string>& resource_ids,
        double time,
        double wait_time,
        const std::string& task_id)
    {
        const auto task_name = flux::node(model_, task_id).name;
        for (const auto& resource_id : resource_ids)
        {
            auto& runtime = resource_runtime(registry, resource_id);
            const auto queue_length = queue_length_for_resource(resource_id);
            update_busy_time(runtime, time);
            ++runtime.in_use;
            ++runtime.allocation_count;
            runtime.total_wait_time += wait_time;
            runtime.max_queue_length = std::max(runtime.max_queue_length, queue_length);
            log_resource_timeline(result, time, runtime, "acquire", queue_length, task_id, task_name);
        }
    }

    void apply_release(
        entt::registry& registry,
        Result& result,
        const std::vector<std::string>& resource_ids,
        double time,
        const std::string& task_id)
    {
        const auto task_name = flux::node(model_, task_id).name;
        for (const auto& resource_id : resource_ids)
        {
            auto& runtime = resource_runtime(registry, resource_id);
            const auto queue_length = queue_length_for_resource(resource_id);
            update_busy_time(runtime, time);
            runtime.in_use = std::max(0, runtime.in_use - 1);
            runtime.max_queue_length = std::max(runtime.max_queue_length, queue_length);
            log_resource_timeline(result, time, runtime, "release", queue_length, task_id, task_name);
        }
    }

    void finalize(entt::registry& registry, Result& result)
    {
        const auto horizon = result.simulation_horizon;
        for (const auto& resource_id : resource_ids_)
        {
            auto& runtime = resource_runtime(registry, resource_id);
            update_busy_time(runtime, horizon);

            const auto busy_time = runtime.busy_unit_time;
            const auto idle_time = std::max(0.0, horizon - busy_time);
            const auto utilization = horizon > 0.0 ? busy_time / horizon : 0.0;
            const auto average_wait_time = runtime.allocation_count > 0 ? runtime.total_wait_time / static_cast<double>(runtime.allocation_count) : 0.0;

            result.reports.resource_summary_rows.push_back(ResourceSummaryRow{
                runtime.resource_id,
                runtime.resource_name,
                runtime.capacity,
                busy_time,
                idle_time,
                utilization,
                runtime.max_queue_length,
                average_wait_time,
                runtime.allocation_count,
            });
        }
    }

    void note_request_enqueued(entt::registry& registry, Result& result, const PendingTaskRequest& request)
    {
        const auto task_name = flux::node(model_, request.task_id).name;
        for (const auto& resource_id : task_resources(request.task_id))
        {
            auto& queue_length = resource_queue_lengths_[resource_id];
            ++queue_length;
            auto& runtime = resource_runtime(registry, resource_id);
            runtime.max_queue_length = std::max(runtime.max_queue_length, queue_length);
            log_resource_timeline(result, request.arrival_time, runtime, "enqueue", queue_length, request.task_id, task_name);
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

private:
    static void log_resource_timeline(
        Result& result,
        double time,
        const ResourceRuntime& runtime,
        const std::string& change_type,
        int queue_length,
        const std::string& task_id,
        const std::string& task_name)
    {
        result.reports.resource_timeline_rows.push_back(ResourceTimelineRow{
            time,
            runtime.resource_id,
            runtime.resource_name,
            change_type,
            runtime.in_use,
            runtime.capacity - runtime.in_use,
            queue_length,
            task_id,
            task_name,
        });
    }

    ResourceRuntime& resource_runtime(entt::registry& registry, const std::string& resource_id)
    {
        return registry.get<ResourceRuntime>(resource_entities_.at(resource_id));
    }

    const ResourceRuntime& resource_runtime(entt::registry& registry, const std::string& resource_id) const
    {
        return registry.get<ResourceRuntime>(resource_entities_.at(resource_id));
    }

    static void update_busy_time(ResourceRuntime& runtime, double time)
    {
        const auto delta = time - runtime.last_update_time;
        if (delta > 0.0)
        {
            runtime.busy_unit_time += static_cast<double>(runtime.in_use > 0 ? 1 : 0) * delta;
        }
        runtime.last_update_time = time;
    }

    const Model& model_;
    std::unordered_map<std::string, entt::entity> resource_entities_;
    std::unordered_map<std::string, int> resource_queue_lengths_;
    std::vector<std::string> resource_ids_;
};

class Engine::TokenManager
{
public:
    explicit TokenManager(const ResourceManager& resources)
        : resources_(resources)
    {
    }

    [[nodiscard]] bool token_has_held_resources(const entt::registry& registry, entt::entity token_entity) const
    {
        return registry.all_of<HeldResources>(token_entity) && !registry.get<HeldResources>(token_entity).resource_ids.empty();
    }

    [[nodiscard]] bool token_has_combine_history(const entt::registry& registry, entt::entity token_entity) const
    {
        return registry.all_of<CombineHistory>(token_entity) && !registry.get<CombineHistory>(token_entity).members.empty();
    }

    [[nodiscard]] const std::vector<std::string>& held_resources(const entt::registry& registry, entt::entity token_entity) const
    {
        if (registry.all_of<HeldResources>(token_entity))
        {
            return registry.get<HeldResources>(token_entity).resource_ids;
        }

        static const std::vector<std::string> empty;
        return empty;
    }

    [[nodiscard]] const CombineHistory& combine_history(const entt::registry& registry, entt::entity token_entity) const
    {
        return registry.get<CombineHistory>(token_entity);
    }

    [[nodiscard]] std::shared_ptr<CombineHistory> snapshot_combine_history(const entt::registry& registry, entt::entity token_entity) const
    {
        if (!registry.all_of<CombineHistory>(token_entity))
        {
            return nullptr;
        }

        return std::make_shared<CombineHistory>(registry.get<CombineHistory>(token_entity));
    }

    [[nodiscard]] RestorableTokenSnapshot snapshot_token(const entt::registry& registry, entt::entity token_entity) const
    {
        return snapshot_token(registry, token_entity, 1, std::nullopt);
    }

    [[nodiscard]] RestorableTokenSnapshot snapshot_token(
        const entt::registry& registry,
        entt::entity token_entity,
        std::size_t restore_count,
        const std::optional<std::string>& quantity) const
    {
        return RestorableTokenSnapshot{registry.get<ProcessToken>(token_entity), snapshot_combine_history(registry, token_entity), restore_count, quantity, held_resources(registry, token_entity)};
    }

    void restore_snapshot_history(entt::registry& registry, entt::entity token_entity, const std::shared_ptr<CombineHistory>& history)
    {
        if (!history)
        {
            if (registry.all_of<CombineHistory>(token_entity))
            {
                registry.remove<CombineHistory>(token_entity);
            }
            return;
        }

        registry.emplace_or_replace<CombineHistory>(token_entity, *history);
    }

    void set_combine_history(entt::registry& registry, entt::entity token_entity, std::vector<RestorableTokenSnapshot> members)
    {
        registry.emplace_or_replace<CombineHistory>(token_entity, CombineHistory{std::move(members)});
    }

    void add_held_resources(entt::registry& registry, entt::entity token_entity, const std::vector<std::string>& resource_ids)
    {
        if (resource_ids.empty())
        {
            return;
        }

        auto& held = registry.get_or_emplace<HeldResources>(token_entity);
        held.resource_ids.insert(held.resource_ids.end(), resource_ids.begin(), resource_ids.end());
        // 保持排序，后续 release 直接用二分判断绑定资源是否被持有。
        std::sort(held.resource_ids.begin(), held.resource_ids.end());
    }

    [[nodiscard]] std::vector<std::string> release_resources_for_task(const entt::registry& registry, entt::entity token_entity, const std::string& task_id) const
    {
        const auto& currently_held = held_resources(registry, token_entity);
        if (currently_held.empty())
        {
            return {};
        }

        const auto& bound_resources = resources_.task_resources(task_id);
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

    void remove_held_resources(entt::registry& registry, entt::entity token_entity, const std::vector<std::string>& resource_ids)
    {
        if (resource_ids.empty() || !registry.all_of<HeldResources>(token_entity))
        {
            return;
        }

        auto& held = registry.get<HeldResources>(token_entity).resource_ids;
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
            registry.remove<HeldResources>(token_entity);
        }
    }

    void enqueue_combine_member(const std::string& task_id, const std::string& group, entt::entity token_entity, std::size_t equivalent_units)
    {
        if (equivalent_units == 0)
        {
            return;
        }

        combine_waiting_[CombineGroupKey{task_id, group}].push_back(WaitingCombineToken{token_entity, equivalent_units});
        auto& state = combine_token_state_[static_cast<std::uint32_t>(entt::to_integral(token_entity))];
        state.waiting_units += equivalent_units;
    }

    [[nodiscard]] std::vector<CombineBatch::Member> take_waiting_combine_members(
        const entt::registry& registry,
        const std::string& task_id,
        const std::string& group,
        std::size_t required_units)
    {
        auto found = combine_waiting_.find(CombineGroupKey{task_id, group});
        if (found == combine_waiting_.end())
        {
            return {};
        }

        auto& waiting = found->second;
        while (!waiting.empty() && !(registry.valid(waiting.front().token) && registry.all_of<ProcessToken>(waiting.front().token)))
        {
            auto& state = combine_token_state_[static_cast<std::uint32_t>(entt::to_integral(waiting.front().token))];
            state.waiting_units = state.waiting_units >= waiting.front().remaining_units ? state.waiting_units - waiting.front().remaining_units : 0;
            if (state.waiting_units == 0 && state.in_flight_units == 0)
            {
                combine_token_state_.erase(static_cast<std::uint32_t>(entt::to_integral(waiting.front().token)));
            }
            waiting.pop_front();
        }
        if (waiting.empty())
        {
            return {};
        }

        std::vector<CombineBatch::Member> members;
        while (required_units > 0 && !waiting.empty())
        {
            auto& front = waiting.front();
            const auto consumed_units = std::min(required_units, front.remaining_units);
            members.push_back(CombineBatch::Member{front.token, consumed_units});

            auto& state = combine_token_state_[static_cast<std::uint32_t>(entt::to_integral(front.token))];
            state.waiting_units -= consumed_units;
            state.in_flight_units += consumed_units;

            front.remaining_units -= consumed_units;
            required_units -= consumed_units;
            if (front.remaining_units == 0)
            {
                waiting.pop_front();
            }
        }
        if (required_units != 0)
        {
            throw std::runtime_error("Combine task reached an invalid waiting state.");
        }
        if (waiting.empty())
        {
            combine_waiting_.erase(found);
        }
        return members;
    }

    void finish_combine_members(entt::registry& registry, const std::vector<CombineBatch::Member>& members)
    {
        for (const auto& member : members)
        {
            const auto key = static_cast<std::uint32_t>(entt::to_integral(member.token));
            auto found = combine_token_state_.find(key);
            if (found == combine_token_state_.end())
            {
                continue;
            }

            auto& state = found->second;
            state.in_flight_units = state.in_flight_units >= member.consumed_units ? state.in_flight_units - member.consumed_units : 0;
            if (state.waiting_units == 0 && state.in_flight_units == 0)
            {
                combine_token_state_.erase(found);
                if (registry.valid(member.token))
                {
                    registry.destroy(member.token);
                }
            }
        }
    }

private:
    const ResourceManager& resources_;
    std::unordered_map<CombineGroupKey, std::deque<WaitingCombineToken>, CombineGroupKeyHash> combine_waiting_;
    std::unordered_map<std::uint32_t, CombineTokenState> combine_token_state_;
};

class Engine::PendingManager
{
public:
    explicit PendingManager(const Model& model)
        : model_(model)
    {
        initialize_task_queue_index();
    }

    [[nodiscard]] bool has_requests() const
    {
        return !pending_requests_.empty();
    }

    [[nodiscard]] bool begin_resolution()
    {
        if (!pending_resolution_needed_)
        {
            return false;
        }

        pending_resolution_needed_ = false;
        return true;
    }

    void note_resolution_needed()
    {
        pending_resolution_needed_ = true;
    }

    void enqueue_request(PendingTaskRequest request, entt::registry& registry, ResourceManager& resources, Result& result);
    void rearm_resource_queues(const std::string& resource_id);
    struct ReadyRequest
    {
        PendingTaskRequest request;
        std::vector<std::string> allocation;
    };

    [[nodiscard]] std::optional<ReadyRequest> next_ready_request(entt::registry& registry, ResourceManager& resources);

private:
    using PendingRequestMap = std::unordered_map<PendingQueueKey, std::deque<PendingTaskRequest>, PendingQueueKeyHash>;

    struct PendingCandidateView
    {
        PendingQueueKey key;
        std::vector<std::string> allocation;
    };

    void initialize_task_queue_index();
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
        const auto found = model_.task_resources.find(task_id);
        if (found != model_.task_resources.end() && found->second.size() == 1)
        {
            // 单资源等待队列直接挂在资源上，多资源等待队列挂在任务自身上。
            return resource_queue_key(found->second.front());
        }

        return task_queue_key(task_id);
    }

    [[nodiscard]] std::optional<PendingCandidateView> next_pending_candidate(entt::registry& registry, ResourceManager& resources);
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

    void discard_invalid_fronts(const PendingQueueKey& key, entt::registry& registry, ResourceManager& resources);
    PendingTaskRequest take_front_request(const PendingQueueKey& key);

    const Model& model_;
    PendingRequestMap pending_requests_;
    std::priority_queue<PendingCandidate> pending_candidates_;
    std::unordered_map<std::string, std::vector<std::string>> task_queue_ids_by_resource_;
    bool pending_resolution_needed_{false};
};

class Engine::RunState
{
public:
    RunState(const Model& model, std::uint64_t seed)
        : model_(model)
        , sampler_(seed)
        , resources_(model)
        , pending_(model)
    {
        resources_.initialize(registry_);
        initialize_task_runtimes();
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

    [[nodiscard]] Result take_result()
    {
        return std::move(result_);
    }

    void resolve_pending(double time)
    {
        if (!pending_.begin_resolution())
        {
            return;
        }

        while (true)
        {
            auto ready = pending_.next_ready_request(registry_, resources_);
            if (!ready.has_value())
            {
                break;
            }

            const auto& node = flux::node(model_, ready->request.task_id);
            start_task(ready->request.token, node, time, ready->allocation, time - ready->request.arrival_time);
        }
    }

    void finalize()
    {
        resources_.finalize(registry_, result_);
        finalize_task_runtimes();
    }

    void schedule_start_events();
    void process_event(const ScheduledEvent& event);
    void handle_generate_entity(const ScheduledEvent& event);
    void handle_arrive_node(const ScheduledEvent& event);
    void handle_finish_task(const ScheduledEvent& event);

private:
    void schedule(ScheduledEvent event)
    {
        queue_.push(std::move(event));
    }

    [[nodiscard]] std::uint64_t next_order()
    {
        return next_order_++;
    }

    [[nodiscard]] std::size_t next_global_seq()
    {
        return next_global_seq_++;
    }

    [[nodiscard]] std::size_t next_type_seq(const std::string& entity_type)
    {
        return entity_type_sequences_[entity_type]++;
    }

    entt::entity create_token(
        std::size_t global_seq,
        const std::string& entity_type,
        double created_at,
        std::string entity_name,
        std::unordered_map<std::string, std::string> properties = {})
    {
        const auto entity = registry_.create();
        registry_.emplace<ProcessToken>(entity, ProcessToken{global_seq, entity_type, created_at, std::move(entity_name), std::move(properties)});
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

    void initialize_task_runtimes()
    {
        for (const auto& [node_id, node] : model_.nodes)
        {
            if (node.type != NodeType::Task)
            {
                continue;
            }

            task_ids_.push_back(node_id);
        }

        std::sort(task_ids_.begin(), task_ids_.end());
        for (const auto& task_id : task_ids_)
        {
            const auto& node = flux::node(model_, task_id);
            task_runtimes_.insert_or_assign(task_id, TaskRuntime{node.id, node.name});
        }
    }

    void finalize_task_runtimes()
    {
        const auto horizon = result_.simulation_horizon;
        for (const auto& task_id : task_ids_)
        {
            auto& runtime = task_runtimes_.at(task_id);
            auto busy_time = runtime.busy_time;
            if (runtime.active_count > 0)
            {
                busy_time += std::max(0.0, horizon - runtime.last_busy_start_time);
            }

            const auto busy_rate = horizon > 0.0 ? busy_time / horizon : 0.0;

            result_.reports.task_summary_rows.push_back(TaskSummaryRow{
                runtime.task_id,
                runtime.task_name,
                runtime.entity_count,
                runtime.queue_stats.count,
                busy_time,
                busy_rate,
                runtime.queue_stats.total_time,
                runtime.queue_stats.average_time(),
                runtime.queue_stats.max_or_zero(),
                runtime.queue_stats.min_or_zero(),
                runtime.process_stats.total_time,
                runtime.process_stats.average_time(),
                runtime.process_stats.max_or_zero(),
                runtime.process_stats.min_or_zero(),
            });
        }
    }

    void schedule_token_to_outgoing(const std::string& node_id, entt::entity token_entity, double time)
    {
        const auto& target_id = model_.outgoing.at(node_id).front();
        schedule(ScheduledEvent{time, next_order(), ScheduledEventType::ArriveNode, target_id, token_entity, std::nullopt});
    }

    void destroy_token(entt::entity entity)
    {
        if (registry_.valid(entity))
        {
            registry_.destroy(entity);
        }
    }

    void log_event(double time, const ProcessToken& token_component, const NodeDefinition& node, const std::string& event_type)
    {
        result_.reports.event_rows.push_back(EventLogRow{
            time,
            std::to_string(token_component.global_seq),
            token_component.entity_name,
            node.id,
            node.name,
            event_type,
        });
    }

    void log_task_timeline(const std::string& task_id, double time, int waiting_delta, int running_delta, int completed_delta)
    {
        auto& task = task_runtimes_.at(task_id);
        task.waiting_count += waiting_delta;
        task.running_count += running_delta;
        task.completed_count += completed_delta;
        result_.reports.task_timeline_rows.push_back(TaskTimelineRow{
            time,
            task.task_id,
            task.task_name,
            task.entity_count,
            task.waiting_count,
            task.running_count,
            task.completed_count,
        });
    }

    void apply_release(const std::vector<std::string>& resource_ids, double time, const std::string& task_id)
    {
        resources_.apply_release(registry_, result_, resource_ids, time, task_id);
        for (const auto& resource_id : resource_ids)
        {
            pending_.rearm_resource_queues(resource_id);
        }

        if (pending_.has_requests())
        {
            pending_.note_resolution_needed();
        }
    }

    void start_task(entt::entity token_entity, const NodeDefinition& node, double time, const std::vector<std::string>& allocation, double wait_time)
    {
        const auto& token_component = token(token_entity);
        if (node.task->type != TaskType::ReleaseResource)
        {
            resources_.apply_allocation(registry_, result_, allocation, time, wait_time, node.id);
        }
        auto& task = task_runtimes_.at(node.id);
        task.queue_stats.note_sample(wait_time);
        if (task.active_count == 0)
        {
            task.last_busy_start_time = time;
        }
        ++task.active_count;
        registry_.emplace_or_replace<ActiveTask>(token_entity, ActiveTask{node.id, allocation, time});

        const auto duration = sampler_.sample(node.task->duration_distribution);
        log_event(time, token_component, node, "task_start");
        log_task_timeline(node.id, time, wait_time > 0.0 ? -1 : 0, +1, 0);

        schedule(ScheduledEvent{time + duration, next_order(), ScheduledEventType::FinishTask, node.id, token_entity, std::nullopt});
        if (node.task->type == TaskType::Split)
        {
            schedule_split_outputs(token_entity, node, time, duration);
        }
    }

    [[nodiscard]] std::string select_exclusive_gateway_target(const NodeDefinition& node, entt::entity token_entity);
    [[nodiscard]] std::vector<entt::entity> create_restored_tokens(const RestorableTokenSnapshot& snapshot);
    [[nodiscard]] std::size_t advance_combine_outputs(const std::string& task_id, const std::string& group, std::size_t equivalent_units, double ratio);
    [[nodiscard]] std::size_t advance_split_outputs(const std::string& task_id, double ratio);
    [[nodiscard]] std::size_t read_positive_integer_property(const ProcessToken& token_component, const std::string& property_name, const std::string& context) const;
    [[nodiscard]] std::size_t combine_equivalent_units(const NodeDefinition& node, entt::entity token_entity) const;
    [[nodiscard]] std::string combine_group_value(const NodeDefinition& node, entt::entity token_entity) const;
    [[nodiscard]] std::size_t combine_output_units(double ratio, std::size_t output_index) const;
    void start_or_enqueue_task(entt::entity token_entity, const NodeDefinition& node, double time);
    void schedule_split_outputs(entt::entity token_entity, const NodeDefinition& node, double start_time, double duration);

    entt::registry registry_;
    const Model& model_;
    Result result_;
    DistributionSampler sampler_;
    Engine::ResourceManager resources_;
    Engine::TokenManager tokens_{resources_};
    Engine::PendingManager pending_;
    std::priority_queue<ScheduledEvent> queue_;
    double current_time_{0.0};
    std::uint64_t next_order_{0};
    std::size_t next_global_seq_{0};
    std::unordered_map<std::string, std::size_t> entity_type_sequences_;
    std::unordered_map<CombineGroupKey, RatioProgress, CombineGroupKeyHash> combine_ratio_progress_;
    std::unordered_map<std::string, RatioProgress> split_ratio_progress_;
    std::unordered_map<std::string, TaskRuntime> task_runtimes_;
    std::vector<std::string> task_ids_;
};

void Engine::PendingManager::enqueue_request(PendingTaskRequest request, entt::registry& registry, ResourceManager& resources, Result& result)
{
    resources.note_request_enqueued(registry, result, request);
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

void Engine::PendingManager::rearm_resource_queues(const std::string& resource_id)
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

std::optional<Engine::PendingManager::ReadyRequest> Engine::PendingManager::next_ready_request(entt::registry& registry, ResourceManager& resources)
{
    auto candidate = next_pending_candidate(registry, resources);
    if (!candidate.has_value())
    {
        return std::nullopt;
    }

    auto request = take_front_request(candidate->key);
    resources.note_request_dequeued(request);
    return ReadyRequest{std::move(request), std::move(candidate->allocation)};
}

void Engine::PendingManager::initialize_task_queue_index()
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

std::optional<Engine::PendingManager::PendingCandidateView> Engine::PendingManager::next_pending_candidate(entt::registry& registry, ResourceManager& resources)
{
    while (!pending_candidates_.empty())
    {
        const auto candidate = pending_candidates_.top();

        // 候选堆允许旧条目残留，通过惰性清理避免在 release 路径上全量扫描。
        discard_invalid_fronts(candidate.key, registry, resources);

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
        auto allocation = resources.allocate_resources_if_possible(registry, request.task_id, node.task->resource_strategy);
        if (allocation.empty())
        {
            pending_candidates_.pop();
            continue;
        }

        return PendingCandidateView{candidate.key, std::move(allocation)};
    }

    return std::nullopt;
}
void Engine::PendingManager::discard_invalid_fronts(const PendingQueueKey& key, entt::registry& registry, ResourceManager& resources)
{
    const auto found = pending_requests_.find(key);
    if (found == pending_requests_.end())
    {
        return;
    }

    auto& requests = found->second;
    auto removed_any = false;
    while (!requests.empty() && !(registry.valid(requests.front().token) && registry.all_of<ProcessToken>(requests.front().token)))
    {
        resources.note_request_dequeued(requests.front());
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

PendingTaskRequest Engine::PendingManager::take_front_request(const PendingQueueKey& key)
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

std::vector<entt::entity> Engine::RunState::create_restored_tokens(const RestorableTokenSnapshot& snapshot)
{
    std::vector<entt::entity> restored_tokens;
    restored_tokens.reserve(snapshot.restore_count);

    if (!snapshot.quantity.has_value())
    {
        const auto restored = create_token(snapshot.token.global_seq, snapshot.token.entity_type, snapshot.token.created_at, snapshot.token.entity_name, snapshot.token.properties);
        tokens_.restore_snapshot_history(registry_, restored, snapshot.history);
        restored_tokens.push_back(restored);
        if (!snapshot.held_resource_ids.empty())
        {
            registry_.emplace<HeldResources>(restored_tokens.back(), HeldResources{snapshot.held_resource_ids});
        }
        return restored_tokens;
    }

    for (std::size_t index = 0; index < snapshot.restore_count; ++index)
    {
        auto properties = snapshot.token.properties;
        properties[*snapshot.quantity] = "1";
        const auto restored = create_token(next_global_seq(), snapshot.token.entity_type, snapshot.token.created_at, snapshot.token.entity_name, std::move(properties));
        tokens_.restore_snapshot_history(registry_, restored, snapshot.history);
        restored_tokens.push_back(restored);
        if (!snapshot.held_resource_ids.empty())
        {
            registry_.emplace<HeldResources>(restored_tokens.back(), HeldResources{snapshot.held_resource_ids});
        }
    }

    return restored_tokens;
}

std::size_t Engine::RunState::advance_combine_outputs(const std::string& task_id, const std::string& group, std::size_t equivalent_units, double ratio)
{
    auto& progress = combine_ratio_progress_[CombineGroupKey{task_id, group}];
    progress.processed_inputs += equivalent_units;

    const auto target_outputs = static_cast<std::size_t>(std::floor((static_cast<long double>(progress.processed_inputs) / static_cast<long double>(ratio)) + 1e-12L));
    if (target_outputs <= progress.emitted_outputs)
    {
        return 0;
    }

    const auto delta = target_outputs - progress.emitted_outputs;
    progress.emitted_outputs = target_outputs;
    return delta;
}

std::size_t Engine::RunState::advance_split_outputs(const std::string& task_id, double ratio)
{
    auto& progress = split_ratio_progress_[task_id];
    ++progress.processed_inputs;

    const auto target_outputs = static_cast<std::size_t>(std::floor((static_cast<long double>(progress.processed_inputs) * static_cast<long double>(ratio)) + 1e-12L));
    if (target_outputs <= progress.emitted_outputs)
    {
        return 0;
    }

    const auto delta = target_outputs - progress.emitted_outputs;
    progress.emitted_outputs = target_outputs;
    return delta;
}

std::size_t Engine::RunState::read_positive_integer_property(const ProcessToken& token_component, const std::string& property_name, const std::string& context) const
{
    const auto found = token_component.properties.find(property_name);
    if (found == token_component.properties.end() || found->second.empty())
    {
        throw std::runtime_error(context + " requires token property '" + property_name + "' to be a positive integer.");
    }

    std::size_t parsed_length = 0;
    unsigned long long parsed_count = 0;
    try
    {
        parsed_count = std::stoull(found->second, &parsed_length);
    }
    catch (const std::exception&)
    {
        throw std::runtime_error(context + " requires token property '" + property_name + "' to be a positive integer.");
    }

    if (parsed_length != found->second.size() || parsed_count == 0 || parsed_count > std::numeric_limits<std::size_t>::max())
    {
        throw std::runtime_error(context + " requires token property '" + property_name + "' to be a positive integer.");
    }

    return static_cast<std::size_t>(parsed_count);
}

std::size_t Engine::RunState::combine_equivalent_units(const NodeDefinition& node, entt::entity token_entity) const
{
    const auto& combine = *node.task->combine;
    if (!combine.use_quantity_property)
    {
        return 1;
    }

    return read_positive_integer_property(token(token_entity), *combine.quantity, "Task '" + node.id + "'");
}

std::string Engine::RunState::combine_group_value(const NodeDefinition& node, entt::entity token_entity) const
{
    const auto& combine = *node.task->combine;
    if (combine.method != CombineMethod::GroupRatio)
    {
        return {};
    }

    const auto& token_component = token(token_entity);
    const auto found = token_component.properties.find(*combine.group);
    if (found == token_component.properties.end() || found->second.empty())
    {
        throw std::runtime_error("Task '" + node.id + "' requires token property '" + *combine.group + "' for property-based combine grouping.");
    }

    return found->second;
}

std::size_t Engine::RunState::combine_output_units(double ratio, std::size_t output_index) const
{
    const auto upper = static_cast<std::size_t>(std::ceil((static_cast<long double>(output_index) * static_cast<long double>(ratio)) - 1e-12L));
    const auto lower = output_index == 1
                           ? 0U
                           : static_cast<std::size_t>(std::ceil((static_cast<long double>(output_index - 1) * static_cast<long double>(ratio)) - 1e-12L));
    return upper - lower;
}

void Engine::RunState::start_or_enqueue_task(entt::entity token_entity, const NodeDefinition& node, double time)
{
    const auto& requested_resources = resources_.task_resources(node.id);
    if (requested_resources.empty())
    {
        start_task(token_entity, node, time, {}, 0.0);
        return;
    }

    if (!pending_.has_requests())
    {
        // 没有历史等待者时允许直接抢占；一旦存在等待队列，必须走统一仲裁路径。
        const auto allocation = resources_.allocate_resources_if_possible(registry_, node.id, node.task->resource_strategy);
        if (!allocation.empty())
        {
            start_task(token_entity, node, time, allocation, 0.0);
            return;
        }
    }
    pending_.enqueue_request(PendingTaskRequest{next_order(), token_entity, node.id, time}, registry_, resources_, result_);
    log_event(time, token(token_entity), node, "task_waiting_for_resources");
    log_task_timeline(node.id, time, +1, 0, 0);
}

void Engine::RunState::schedule_split_outputs(entt::entity token_entity, const NodeDefinition& node, double start_time, double duration)
{
    if (node.task->type != TaskType::Split)
    {
        return;
    }

    std::vector<entt::entity> outputs;
    if (node.task->split->method == SplitMethod::Ratio)
    {
        const auto output_count = advance_split_outputs(node.id, node.task->split->ratio);
        outputs.reserve(output_count);
        for (std::size_t index = 0; index < output_count; ++index)
        {
            const auto seq = next_global_seq();
            const auto type_seq = next_type_seq(node.task->split->entity_type);
            const auto entity_name = node.name + "-" + node.task->split->entity_type + "-" + std::to_string(type_seq);
            const auto child = create_token(seq, node.task->split->entity_type, start_time, entity_name);
            outputs.push_back(child);
        }
    }
    else if (node.task->split->method == SplitMethod::Quantity)
    {
        const auto& parent = token(token_entity);
        const auto& quantity = node.task->split->quantity.value();
        const auto output_count = read_positive_integer_property(parent, quantity, "Task '" + node.id + "'");
        outputs.reserve(output_count);
        for (std::size_t index = 0; index < output_count; ++index)
        {
            const auto seq = next_global_seq();
            const auto type_seq = next_type_seq(parent.entity_type);
            const auto entity_name = node.name + "-" + parent.entity_type + "-" + std::to_string(type_seq);
            const auto child = create_token(seq, parent.entity_type, start_time, entity_name, parent.properties);
            outputs.push_back(child);
        }
    }
    else
    {
        if (!tokens_.token_has_combine_history(registry_, token_entity))
        {
            throw std::runtime_error("Task '" + node.id + "' requires a previously combined entity when '_method=restore'.");
        }

        const auto& history = tokens_.combine_history(registry_, token_entity);
        outputs.reserve(history.members.size());
        for (const auto& snapshot : history.members)
        {
            auto restored = create_restored_tokens(snapshot);
            outputs.insert(outputs.end(), restored.begin(), restored.end());
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

void Engine::RunState::schedule_start_events()
{
    for (const auto& start_id : model_.start_node_ids)
    {
        const auto& start_node = flux::node(model_, start_id);
        if (start_node.generator->type == InitiatorType::External)
        {
            for (std::size_t index = 0; index < start_node.generator->external_records.size(); ++index)
            {
                const auto& record = start_node.generator->external_records[index];
                schedule(ScheduledEvent{record.time, next_order(), ScheduledEventType::GenerateEntity, start_id, entt::null, index});
            }
            continue;
        }

        double next_time = 0.0;
        for (std::size_t index = 0; index < start_node.generator->entity_count; ++index)
        {
            schedule(ScheduledEvent{next_time, next_order(), ScheduledEventType::GenerateEntity, start_id, entt::null, std::nullopt});
            if (index + 1 < start_node.generator->entity_count)
            {
                next_time += sampler_.sample(start_node.generator->interval_distribution);
            }
        }
    }
}

void Engine::RunState::process_event(const ScheduledEvent& event)
{
    switch (event.type)
    {
        case ScheduledEventType::GenerateEntity:
            handle_generate_entity(event);
            break;
        case ScheduledEventType::ArriveNode:
            handle_arrive_node(event);
            break;
        case ScheduledEventType::FinishTask:
            handle_finish_task(event);
            break;
        default:
            throw std::runtime_error("unreachable");
    }
}

void Engine::RunState::handle_generate_entity(const ScheduledEvent& event)
{
    const auto& start_node = flux::node(model_, event.node_id);
    std::unordered_map<std::string, std::string> properties;
    if (event.external_record_index.has_value())
    {
        properties = start_node.generator->external_records.at(*event.external_record_index).properties;
    }

    const auto seq = next_global_seq();
    const auto type_seq = next_type_seq(start_node.generator->entity_type);
    const auto entity_name = start_node.name + "-" + start_node.generator->entity_type + "-" + std::to_string(type_seq);
    const auto token_entity = create_token(seq, start_node.generator->entity_type, event.time, entity_name, std::move(properties));
    const auto token_component = token(token_entity);
    ++result_.generated_entities;
    log_event(event.time, token_component, start_node, "entity_generated");
    schedule_token_to_outgoing(start_node.id, token_entity, event.time);
}

void Engine::RunState::handle_arrive_node(const ScheduledEvent& event)
{
    if (!token_valid(event.token))
    {
        return;
    }

    const auto& node = flux::node(model_, event.node_id);
    const auto token_component = token(event.token);

    if (node.type == NodeType::Task)
    {
        if (node.task->type != TaskType::Combine)
        {
            ++task_runtimes_.at(node.id).entity_count;
        }
        log_event(event.time, token_component, node, "task_arrive");

        if (node.task->type == TaskType::Combine)
        {
            const auto group = combine_group_value(node, event.token);
            const auto equivalent_units = combine_equivalent_units(node, event.token);
            tokens_.enqueue_combine_member(node.id, group, event.token, equivalent_units);

            const auto& combine = *node.task->combine;
            const auto output_count = advance_combine_outputs(node.id, group, equivalent_units, combine.ratio);
            if (output_count == 0)
            {
                return;
            }

            auto& progress = combine_ratio_progress_[CombineGroupKey{node.id, group}];
            const auto first_output_index = progress.emitted_outputs - output_count + 1;
            for (std::size_t output_offset = 0; output_offset < output_count; ++output_offset)
            {
                const auto output_index = first_output_index + output_offset;
                const auto required_units = combine_output_units(combine.ratio, output_index);
                const auto members = tokens_.take_waiting_combine_members(registry_, node.id, group, required_units);
                if (members.empty())
                {
                    throw std::runtime_error("Task '" + node.id + "' reached an invalid combine state.");
                }

                std::vector<RestorableTokenSnapshot> snapshots;
                snapshots.reserve(members.size());
                for (const auto& member : members)
                {
                    if (combine.quantity.has_value())
                    {
                        snapshots.push_back(tokens_.snapshot_token(registry_, member.token, member.consumed_units, combine.quantity));
                    }
                    else
                    {
                        snapshots.push_back(tokens_.snapshot_token(registry_, member.token));
                    }
                }

                const auto seq = next_global_seq();
                const auto type_seq = next_type_seq(combine.entity_type);
                const auto batch_entity_name = node.name + "-" + combine.entity_type + "-" + std::to_string(type_seq);
                const auto batch_token = create_token(seq, combine.entity_type, event.time, batch_entity_name);
                registry_.emplace<CombineBatch>(batch_token, CombineBatch{members});
                tokens_.set_combine_history(registry_, batch_token, std::move(snapshots));

                ++task_runtimes_.at(node.id).entity_count;
                start_or_enqueue_task(batch_token, node, event.time);
            }
            return;
        }

        if (node.task->type == TaskType::ReleaseResource)
        {
            start_task(event.token, node, event.time, tokens_.release_resources_for_task(registry_, event.token, node.id), 0.0);
            return;
        }

        start_or_enqueue_task(event.token, node, event.time);
        return;
    }

    if (node.type == NodeType::EndEvent)
    {
        log_event(event.time, token_component, node, "entity_exit");
        ++result_.completed_entities;
        destroy_token(event.token);
        return;
    }

    if (node.type == NodeType::ExclusiveGateway)
    {
        const auto selected_target = select_exclusive_gateway_target(node, event.token);
        log_event(event.time, token_component, node, "gateway_route");
        schedule(ScheduledEvent{event.time, next_order(), ScheduledEventType::ArriveNode, selected_target, event.token, std::nullopt});
        return;
    }
}

void Engine::RunState::handle_finish_task(const ScheduledEvent& event)
{
    if (!token_valid(event.token) || !registry_.all_of<ActiveTask>(event.token))
    {
        return;
    }

    const auto& node = flux::node(model_, event.node_id);
    const auto token_component = token(event.token);
    const auto active_task = registry_.get<ActiveTask>(event.token);
    const auto task_type = node.task->type;

    auto& task = task_runtimes_.at(node.id);
    const auto process_time = std::max(0.0, event.time - active_task.start_time);
    task.process_stats.note_sample(process_time);

    if (task.active_count == 0)
    {
        throw std::runtime_error("Task '" + node.id + "' reached an invalid active state.");
    }

    --task.active_count;
    if (task.active_count == 0)
    {
        task.busy_time += std::max(0.0, event.time - task.last_busy_start_time);
    }

    if (task_type != TaskType::AcquireResource)
    {
        apply_release(active_task.allocated_resources, event.time, node.id);
    }

    if (task_type == TaskType::Combine)
    {
        if (registry_.all_of<CombineBatch>(event.token))
        {
            const auto members = registry_.get<CombineBatch>(event.token).members;
            tokens_.finish_combine_members(registry_, members);
            registry_.remove<CombineBatch>(event.token);
        }
    }
    else if (task_type == TaskType::AcquireResource)
    {
        tokens_.add_held_resources(registry_, event.token, active_task.allocated_resources);
    }
    else if (task_type == TaskType::ReleaseResource)
    {
        tokens_.remove_held_resources(registry_, event.token, active_task.allocated_resources);
    }

    if (task_type == TaskType::Transport)
    {
        result_.total_transport_distance += node.task->distance;
    }
    log_event(event.time, token_component, node, "task_finish");
    log_task_timeline(node.id, event.time, 0, -1, +1);
    registry_.remove<ActiveTask>(event.token);

    if (task_type == TaskType::Split)
    {
        destroy_token(event.token);
        return;
    }

    schedule_token_to_outgoing(node.id, event.token, event.time);
}

std::string Engine::RunState::select_exclusive_gateway_target(const NodeDefinition& node, entt::entity token_entity)
{
    if (!node.gateway_criteria.has_value())
    {
        throw std::runtime_error("Exclusive gateway '" + node.id + "' must define routing criteria before execution.");
    }

    if (*node.gateway_criteria == GatewayCriteria::Group)
    {
        if (!node.group.has_value() || node.group->empty())
        {
            throw std::runtime_error("Exclusive gateway '" + node.id + "' must define '_group' when '_criteria=group'.");
        }

        const auto& token_component = token(token_entity);
        const auto property = token_component.properties.find(*node.group);
        if (property == token_component.properties.end())
        {
            throw std::runtime_error("Entity '" + token_component.entity_name + "' is missing gateway property '" + *node.group + "' for exclusive gateway '" + node.id + "'.");
        }

        const auto& flow_ids = model_.outgoing_flow_ids.at(node.id);
        for (const auto& flow_id : flow_ids)
        {
            const auto& candidate = flux::flow(model_, flow_id);
            if (candidate.group_value == property->second)
            {
                return candidate.target_id;
            }
        }

        throw std::runtime_error("Entity '" + token_component.entity_name + "' does not match any outgoing flow for exclusive gateway '" + node.id + "'.");
    }

    if (*node.gateway_criteria != GatewayCriteria::Weight)
    {
        throw std::runtime_error("Unsupported exclusive gateway routing criteria.");
    }

    const auto& flow_ids = model_.outgoing_flow_ids.at(node.id);
    if (flow_ids.size() == 1)
    {
        return flux::flow(model_, flow_ids.front()).target_id;
    }

    double total_weight = 0.0;
    for (const auto& flow_id : flow_ids)
    {
        total_weight += flux::flow(model_, flow_id).weight.value_or(0.0);
    }

    const auto threshold = sampler_.sample_uniform(0.0, total_weight);
    double cumulative_weight = 0.0;
    for (const auto& flow_id : flow_ids)
    {
        const auto& candidate = flux::flow(model_, flow_id);
        cumulative_weight += candidate.weight.value_or(0.0);
        if (threshold < cumulative_weight)
        {
            return candidate.target_id;
        }
    }

    return flux::flow(model_, flow_ids.back()).target_id;
}

Result Engine::run(const Model& model, std::uint64_t seed)
{
    RunState state(model, seed);

    state.schedule_start_events();

    while (state.has_events())
    {
        const auto batch_time = state.next_event_time();
        // 事件队列按 (time, order) 排序，这里按时间批处理来稳定同一时刻的资源仲裁结果。
        do
        {
            const auto event = state.next_event();
            state.process_event(event);
        } while (state.has_events() && state.next_event_time() == batch_time);

        state.resolve_pending(batch_time);
    }

    state.finalize();
    return state.take_result();
}

} // namespace flux
