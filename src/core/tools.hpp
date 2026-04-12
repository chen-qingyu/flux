#pragma once

#include <string>

#include "model.hpp"

namespace flux
{

// 这些辅助函数为只读索引访问提供统一报错语义。
[[nodiscard]] const NodeDefinition& node(const Model& model, const std::string& node_id);
[[nodiscard]] const SequenceFlowDefinition& flow(const Model& model, const std::string& flow_id);
[[nodiscard]] const ResourceDefinition& resource(const Model& model, const std::string& resource_id);

} // namespace flux