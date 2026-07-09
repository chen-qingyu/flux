#pragma once

#include <filesystem>
#include <string>

#include "model.hpp"

namespace flux
{

class Parser
{
public:
    static Model parse(const std::string& model_content,
                       const std::filesystem::path& external_dir = "");

private:
    class ParseSession;
};

} // namespace flux
