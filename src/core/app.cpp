#include "app.hpp"

#include <chrono>
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
    const auto start_time = std::chrono::steady_clock::now();
    const auto input_file = std::filesystem::path{file_path};
    const auto output_dir = std::filesystem::path{"output"};
    const auto input_stem = input_file.stem().string();
    spdlog::info("Simulation starting...");
    spdlog::info("Input: {}", file_path);
    spdlog::info("Output directory: {}", output_dir.string());
    spdlog::info("Seed: {}", seed);

    Parser parser;
    const auto model = parser.parse(input_file);
    Engine engine;
    const auto result = engine.run(model, seed);
    write_reports(output_dir, result.reports, input_stem);

    const auto end_time = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration<double>(end_time - start_time).count();

    spdlog::info("Simulation complete.");
    spdlog::info("Generated entities: {}", result.generated_entities);
    spdlog::info("Completed entities: {}", result.completed_entities);
    spdlog::info("Simulation horizon: {:.3f}", result.simulation_horizon);
    spdlog::info("Total transport distance: {:.3f}", result.total_transport_distance);
    spdlog::info("Execution time: {:.3f} s", duration);
}

} // namespace flux