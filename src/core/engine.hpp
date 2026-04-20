#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <entt/entt.hpp>

#include "model.hpp"
#include "reporter.hpp"

namespace flux
{

struct Result
{
    ReportBundle reports;
    double simulation_horizon{0.0};
    std::size_t generated_entities{0};
    std::size_t completed_entities{0};
    double total_transport_distance{0.0};
};

class Engine
{
public:
    static Result run(const Model& model, std::uint64_t seed = 42);
};

} // namespace flux