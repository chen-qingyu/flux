#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>

#include "core/app.hpp"

int main(int argc, char** argv)
{
    constexpr std::uint64_t default_seed = 42;
    argparse::ArgumentParser program("flux", "1.0.0");
    program.add_argument("file")
        .help("Path to the BPMN file to simulate.");
    program.add_argument("--seed")
        .default_value(default_seed)
        .scan<'u', std::uint64_t>()
        .help("Deterministic random seed used by the simulator.");
    program.add_argument("--output")
        .default_value(std::string{"output"})
        .help("Directory to write CSV reports into.");

    try
    {
        program.parse_args(argc, argv);

        const auto file_path = program.get<std::string>("file");
        const auto model_name = std::filesystem::path(file_path).stem().string();
        const auto random_seed = program.get<std::uint64_t>("--seed");
        const auto output_dir = program.get<std::string>("--output");

        std::ifstream file_stream(file_path);
        if (!file_stream)
        {
            throw std::runtime_error("Failed to open file '" + file_path + "'.");
        }
        std::ostringstream buffer;
        buffer << file_stream.rdbuf();

        flux::run(model_name, buffer.str(), output_dir, "data/external", random_seed);
    }
    catch (const std::exception& exception)
    {
        spdlog::error("Simulation failed: {}", exception.what());
        return 1;
    }

    return 0;
}
