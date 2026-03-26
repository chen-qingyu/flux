#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>

#include "engine.hpp"
#include "parser.hpp"
#include "reporter.hpp"

namespace
{

struct CliOptions
{
    std::filesystem::path input_path;
    std::filesystem::path output_dir{"output"};
    std::uint64_t seed{42};
};

CliOptions parse_args(int argc, char** argv)
{
    CliOptions options;

    argparse::ArgumentParser program("flux", "0.1.0");

    program.add_argument("--input")
        .required()
        .help("Path to the BPMN model to simulate.");

    program.add_argument("--output")
        .default_value(options.output_dir.string())
        .help("Directory where CSV reports will be written.");

    program.add_argument("--seed")
        .default_value(options.seed)
        .scan<'u', std::uint64_t>()
        .help("Deterministic random seed used by the simulator.");

    program.parse_args(argc, argv);

    options.input_path = program.get<std::string>("--input");
    options.output_dir = program.get<std::string>("--output");
    options.seed = program.get<std::uint64_t>("--seed");
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
        spdlog::info("Seed: {}", cli.seed);
        spdlog::info("Generated entities: {}", result.generated_entities);
        spdlog::info("Completed entities: {}", result.completed_entities);
        spdlog::info("Simulation horizon: {:.3f}", result.simulation_horizon);
        return 0;
    }
    catch (const std::exception& exception)
    {
        spdlog::error("Simulation failed: {}", exception.what());
        return 1;
    }
}
