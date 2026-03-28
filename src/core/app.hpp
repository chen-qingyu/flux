#pragma once

#include <cstdint>
#include <string>

namespace flux
{

void run(const std::string& file_path, std::uint64_t seed = 42);

} // namespace flux