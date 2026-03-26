#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include "bpmn_parser.hpp"
#include "csv_reporting.hpp"
#include "simulation_engine.hpp"

namespace
{

struct CliOptions
{
    std::filesystem::path input_path{"data/demo.bpmn"};
    std::filesystem::path output_dir{"output"};
    std::uint64_t seed{42};
};

CliOptions parse_args(int argc, char** argv)
{
    CliOptions options;

    for (int index = 1; index < argc; ++index)
    {
        const std::string arg = argv[index];
        if (arg == "--input" && index + 1 < argc)
        {
            options.input_path = argv[++index];
            continue;
        }
        if (arg == "--output" && index + 1 < argc)
        {
            options.output_dir = argv[++index];
            continue;
        }
        if (arg == "--seed" && index + 1 < argc)
        {
            options.seed = static_cast<std::uint64_t>(std::stoull(argv[++index]));
            continue;
        }
        if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: flux [--input path/to/model.bpmn] [--output output_dir] [--seed 42]\n";
            std::exit(0);
        }

        throw std::runtime_error("Unknown argument: " + arg);
    }

    return options;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const auto cli = parse_args(argc, argv);
        flux::BpmnParser parser;
        const auto model = parser.parse(cli.input_path);

        flux::SimulationEngine engine;
        const auto result = engine.run(model, flux::SimulationOptions{cli.seed});
        flux::write_reports(cli.output_dir, result.reports);

        spdlog::info("Simulation complete.");
        spdlog::info("Input: {}", cli.input_path.string());
        spdlog::info("Output directory: {}", cli.output_dir.string());
        spdlog::info("Generated entities: {}", result.generated_entities);
        spdlog::info("Completed entities: {}", result.completed_entities);
        spdlog::info("Simulation horizon: {}", flux::format_time(result.simulation_horizon));
        return 0;
    }
    catch (const std::exception& exception)
    {
        spdlog::error("Simulation failed: {}", exception.what());
        return 1;
    }
}
