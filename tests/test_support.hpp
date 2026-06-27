#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../src/core/engine.hpp"
#include "../src/core/parser.hpp"
#include "../src/core/reporter.hpp"
#include "../src/core/tools.hpp"

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

inline Model parse_file(const std::filesystem::path& file_path)
{
    std::ifstream stream(file_path);
    if (!stream)
    {
        throw std::runtime_error("Failed to open file: " + file_path.string());
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return Parser::parse(buffer.str());
}

inline Result run_model(const std::filesystem::path& model_path, std::uint64_t seed = 42)
{
    const auto model = parse_file(model_path);
    return Engine::run(model, seed);
}

inline void require_report_matches(const std::filesystem::path& model_path)
{
    const auto result = run_model(model_path);
    const auto golden_prefix = model_path.stem().string();

    const auto project_root = std::filesystem::current_path();
    const auto output_root = project_root / "output" / golden_prefix;
    std::filesystem::create_directories(output_root);
    Reporter::report(output_root, result.reports);

    REQUIRE(read_text(output_root / "entity_events.csv") == read_text(project_root / "data" / "golden" / (golden_prefix + "_entity_events.csv")));
    REQUIRE(read_text(output_root / "resource_timeline.csv") == read_text(project_root / "data" / "golden" / (golden_prefix + "_resource_timeline.csv")));
    REQUIRE(read_text(output_root / "resource_summary.csv") == read_text(project_root / "data" / "golden" / (golden_prefix + "_resource_summary.csv")));
    REQUIRE(read_text(output_root / "task_summary.csv") == read_text(project_root / "data" / "golden" / (golden_prefix + "_task_summary.csv")));
    REQUIRE(read_text(output_root / "task_timeline.csv") == read_text(project_root / "data" / "golden" / (golden_prefix + "_task_timeline.csv")));
}

inline std::vector<EventLogRow> select_events(const Result& result, const std::string& event_type)
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

inline double require_event_time(const Result& result, const std::string& event_type, const std::string& node_id)
{
    for (const auto& row : result.reports.event_rows)
    {
        if (row.event_type == event_type && row.node_id == node_id)
        {
            return row.time;
        }
    }

    FAIL("missing event row");
    return 0.0;
}

} // namespace flux::test_support