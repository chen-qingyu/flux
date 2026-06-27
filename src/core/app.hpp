#pragma once

#include <cstdint>
#include <string>

namespace flux
{

void run(const std::string& model_name,
         const std::string& model_content,
         const std::string& output_dir = "output",
         const std::string& external_dir = "data/external",
         std::uint64_t random_seed = 42);

} // namespace flux