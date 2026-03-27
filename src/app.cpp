#include "app.hpp"

#include <filesystem>
#include <string>

#include <spdlog/spdlog.h>

#include "engine.hpp"
#include "parser.hpp"
#include "reporter.hpp"

namespace flux
{

void run(const std::string& file_path, std::uint64_t seed)
{
    const auto input_file = std::filesystem::path{file_path};
    const auto output_dir = std::filesystem::path{"output"};
    const auto input_stem = input_file.stem().string();

    BpmnParser parser;
    const auto model = parser.parse(input_file);

    SimulationEngine engine;
    const auto result = engine.run(model, SimulationOptions{seed});
    write_reports(output_dir, result.reports, input_stem);

    spdlog::info("Simulation complete.");
    spdlog::info("Input: {}", file_path);
    spdlog::info("Output directory: {}", output_dir.string());
    spdlog::info("Seed: {}", seed);
    spdlog::info("Generated entities: {}", result.generated_entities);
    spdlog::info("Completed entities: {}", result.completed_entities);
    spdlog::info("Simulation horizon: {:.3f}", result.simulation_horizon);
}

} // namespace flux