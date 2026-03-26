#pragma once

#include <string>

#include "model.hpp"

namespace flux
{

[[nodiscard]] const NodeDefinition& node(const SimulationModel& model, const std::string& node_id);
[[nodiscard]] const ResourceDefinition& resource(const SimulationModel& model, const std::string& resource_id);

} // namespace flux