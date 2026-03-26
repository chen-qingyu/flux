#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine.hpp"
#include "parser.hpp"
#include "reporter.hpp"
#include "tools.hpp"

namespace flux::test_support
{

inline std::string read_text(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    auto text = std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());

    std::string normalized;
    normalized.reserve(text.size());
    for (char ch : text)
    {
        if (ch != '\r')
        {
            normalized.push_back(ch);
        }
    }

    while (!normalized.empty() && normalized.back() == '\n')
    {
        normalized.pop_back();
    }

    return normalized;
}

inline SimulationModel parse_model(const std::filesystem::path& model_path)
{
    BpmnParser parser;
    return parser.parse(model_path);
}

inline SimulationResult run_model(const std::filesystem::path& model_path, std::uint64_t seed = 42)
{
    const auto model = parse_model(model_path);
    SimulationEngine engine;
    return engine.run(model, SimulationOptions{seed});
}

inline void require_report_matches(const std::filesystem::path& model_path, const std::string& golden_prefix)
{
    const auto result = run_model(model_path);

    const auto temp_root = std::filesystem::temp_directory_path() / "flux-tests" / golden_prefix;
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);
    write_reports(temp_root, result.reports);

    const auto project_root = std::filesystem::current_path();
    REQUIRE(read_text(temp_root / "events.csv") == read_text(project_root / "tests" / "golden" / (golden_prefix + "_events.csv")));
    REQUIRE(read_text(temp_root / "resource_timeline.csv") == read_text(project_root / "tests" / "golden" / (golden_prefix + "_resource_timeline.csv")));
    REQUIRE(read_text(temp_root / "resource_summary.csv") == read_text(project_root / "tests" / "golden" / (golden_prefix + "_resource_summary.csv")));
}

inline std::vector<EventLogRow> select_events(const SimulationResult& result, const std::string& event_type)
{
    std::vector<EventLogRow> rows;
    for (const auto& row : result.reports.event_rows)
    {
        if (row.event_type == event_type)
        {
            rows.push_back(row);
        }
    }
    return rows;
}

} // namespace flux::test_support