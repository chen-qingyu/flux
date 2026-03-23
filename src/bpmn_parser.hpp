#pragma once

#include <filesystem>

#include "simulation_model.hpp"

namespace flux
{

class IModelParser
{
public:
    virtual ~IModelParser() = default;
    virtual SimulationModel parse(const std::filesystem::path& input_path) const = 0;
};

class BpmnParser final : public IModelParser
{
public:
    SimulationModel parse(const std::filesystem::path& input_path) const override;
};

} // namespace flux