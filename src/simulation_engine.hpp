#pragma once

#include <cstdint>

#include "csv_reporting.hpp"
#include "simulation_model.hpp"

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
};

} // namespace flux