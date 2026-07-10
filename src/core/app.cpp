#include "app.hpp"

#include <chrono>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include "engine.hpp"
#include "parser.hpp"
#include "reporter.hpp"

namespace flux
{

void run(const std::string& model_name,
         const std::string& model_content,
         const std::string& output_dir,
         const std::string& external_dir,
         std::uint64_t random_seed)
{
    const auto start_time = std::chrono::steady_clock::now();
    spdlog::info("Input: {}", model_name);
    spdlog::info("Output directory: {}", output_dir);
    spdlog::info("Seed: {}", random_seed);
    spdlog::info("Simulation starting...");

    try
    {
        const auto model = Parser::parse(model_content, external_dir);
        const auto result = Engine::run(model, random_seed);
        Reporter::report(output_dir, result.reports, model_name);

        const auto end_time = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration<double>(end_time - start_time).count();
        spdlog::info("Simulation complete.");
        spdlog::info("Generated entities: {}", result.generated_entities);
        spdlog::info("Completed entities: {}", result.completed_entities);
        spdlog::info("Simulation horizon: {:.3f}", result.simulation_horizon);
        spdlog::info("Total transport distance: {:.3f}", result.total_transport_distance);
        spdlog::info("Execution time: {:.3f} s", duration);
    }
    catch (const std::exception& e)
    {
        spdlog::error("Simulation failed: {}", e.what());
        throw;
    }
}

} // namespace flux
