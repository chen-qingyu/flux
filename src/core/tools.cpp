#include "tools.hpp"

#include <stdexcept>

namespace flux
{

const NodeDefinition& node(const Model& model, const std::string& node_id)
{
    if (const auto found = model.nodes.find(node_id); found != model.nodes.end())
    {
        return found->second;
    }
    throw std::runtime_error("Unknown node id: " + node_id);
}

const SequenceFlowDefinition& flow(const Model& model, const std::string& flow_id)
{
    if (const auto found = model.flow_indexes.find(flow_id); found != model.flow_indexes.end())
    {
        return model.flows.at(found->second);
    }
    throw std::runtime_error("Unknown flow id: " + flow_id);
}

const ResourceDefinition& resource(const Model& model, const std::string& resource_id)
{
    if (const auto found = model.resources.find(resource_id); found != model.resources.end())
    {
        return found->second;
    }
    throw std::runtime_error("Unknown resource id: " + resource_id);
}

} // namespace flux