#pragma once

#include <filesystem>

#include "model.hpp"

namespace flux
{

class Parser
{
public:
    Model parse(const std::filesystem::path& file_path) const;

private:
    class ParseSession;
};

} // namespace flux