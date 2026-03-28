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

inline Model parse_model(const std::filesystem::path& model_path)
{
    Parser parser;
    return parser.parse(model_path);
}

inline Result run_model(const std::filesystem::path& model_path, std::uint64_t seed = 42)
{
    const auto model = parse_model(model_path);
    Engine engine;
    return engine.run(model, Options{seed});
}

inline void require_report_matches(const std::filesystem::path& model_path, const std::string& golden_prefix)
{
    const auto result = run_model(model_path);

    const auto project_root = std::filesystem::current_path();
    const auto output_root = project_root / "output" / golden_prefix;
    std::filesystem::create_directories(output_root);
    write_reports(output_root, result.reports);

    REQUIRE(read_text(output_root / "events.csv") == read_text(project_root / "data" / "golden" / (golden_prefix + "_events.csv")));
    REQUIRE(read_text(output_root / "resource_timeline.csv") == read_text(project_root / "data" / "golden" / (golden_prefix + "_resource_timeline.csv")));
    REQUIRE(read_text(output_root / "resource_summary.csv") == read_text(project_root / "data" / "golden" / (golden_prefix + "_resource_summary.csv")));
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

} // namespace flux::test_support