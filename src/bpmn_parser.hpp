#pragma once

#include <filesystem>

#include "simulation_model.hpp"

namespace flux
{

class BpmnParser
{
public:
    SimulationModel parse(const std::filesystem::path& input_path) const;

private:
    class ParseSession;
};

} // namespace flux