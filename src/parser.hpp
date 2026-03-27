#pragma once

#include <filesystem>

#include "model.hpp"

namespace flux
{

class BpmnParser
{
public:
    SimulationModel parse(const std::filesystem::path& file_path) const;

private:
    class ParseSession;
};

} // namespace flux