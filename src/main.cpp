#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>

#include "app.hpp"

namespace
{

struct CliOptions
{
    std::filesystem::path file_path;
    std::uint64_t seed{42};
};

CliOptions parse_args(int argc, char** argv)
{
    CliOptions options;

    argparse::ArgumentParser program("flux", "0.1.0");

    program.add_argument("--file")
        .required()
        .help("Path to the BPMN model to simulate.");

    program.add_argument("--seed")
        .default_value(options.seed)
        .scan<'u', std::uint64_t>()
        .help("Deterministic random seed used by the simulator.");

    program.parse_args(argc, argv);

    options.file_path = program.get<std::string>("--file");
    options.seed = program.get<std::uint64_t>("--seed");
    return options;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const auto cli = parse_args(argc, argv);
        flux::run(cli.file_path.string(), cli.seed);
        return 0;
    }
    catch (const std::exception& exception)
    {
        spdlog::error("Simulation failed: {}", exception.what());
        return 1;
    }
}
