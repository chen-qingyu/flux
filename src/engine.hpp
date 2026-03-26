#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <entt/entt.hpp>

#include "model.hpp"
#include "reporter.hpp"

namespace flux
{

struct SimulationOptions
{
    std::uint64_t seed{42};
};

struct SimulationResult
{
    ReportBundle reports;
    double simulation_horizon{0.0};
    std::size_t generated_entities{0};
    std::size_t completed_entities{0};
};

class SimulationEngine
{
public:
    SimulationResult run(const SimulationModel& model, const SimulationOptions& options = {});

private:
    struct ScheduledEvent;
    class RunState;

    void schedule_start_events(RunState& state) const;
    void process_event(RunState& state, const ScheduledEvent& event) const;
    void handle_generate_entity(RunState& state, const ScheduledEvent& event) const;
    void handle_arrive_node(RunState& state, const ScheduledEvent& event) const;
    void handle_finish_task(RunState& state, const ScheduledEvent& event) const;
    void handle_parallel_gateway(RunState& state, const ScheduledEvent& event) const;
    void continue_parallel_gateway(RunState& state, const ScheduledEvent& event, std::size_t outgoing_count, entt::entity token_entity) const;
};

} // namespace flux