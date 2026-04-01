#pragma once

#include <string>

#include "model.hpp"

namespace flux
{

[[nodiscard]] const NodeDefinition& node(const Model& model, const std::string& node_id);
[[nodiscard]] const SequenceFlowDefinition& flow(const Model& model, const std::string& flow_id);
[[nodiscard]] const ResourceDefinition& resource(const Model& model, const std::string& resource_id);

} // namespace flux