#pragma once

#include <filesystem>

#include "model.hpp"

namespace flux
{

class Parser
{
public:
    static Model parse(const std::filesystem::path& file_path);

private:
    class ParseSession;
};

} // namespace flux