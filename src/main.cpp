#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>

#include "app.hpp"

int main(int argc, char** argv)
{
    constexpr std::uint64_t default_seed = 42;
    argparse::ArgumentParser program("flux", "0.1.0");
    program.add_argument("file")
        .help("Path to the BPMN file to simulate.");
    program.add_argument("--seed")
        .default_value(default_seed)
        .scan<'u', std::uint64_t>()
        .help("Deterministic random seed used by the simulator.");

    try
    {
        program.parse_args(argc, argv);

        const auto file_path = std::filesystem::path(program.get<std::string>("file")).string();
        const auto seed = program.get<std::uint64_t>("--seed");

        flux::run(file_path, seed);
    }
    catch (const std::exception& exception)
    {
        spdlog::error("Simulation failed: {}", exception.what());
        return 1;
    }

    return 0;
}
