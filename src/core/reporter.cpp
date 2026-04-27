#include "reporter.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <tuple>

#include <csv.hpp>
#include <spdlog/spdlog.h>

namespace flux
{
namespace
{

constexpr int TIME_PRECISION = 2;
constexpr int RATIO_PRECISION = 4;

std::string format_fixed(double value, int precision)
{
    return fmt::format("{:.{}f}", value, precision);
}

std::ofstream open_csv_file(const std::filesystem::path& path)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("Failed to open " + path.filename().string() + " for writing.");
    }
    return stream;
}

std::filesystem::path csv_path(const std::filesystem::path& output_directory, const std::string& base_name, const std::string& file_suffix)
{
    if (file_suffix.empty())
    {
        return output_directory / (base_name + ".csv");
    }
    return output_directory / fmt::format("{}_{}.csv", base_name, file_suffix);
}

} // namespace

void Reporter::write_events(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix)
{
    auto stream = open_csv_file(csv_path(output_directory, "events", file_suffix));
    auto writer = csv::make_csv_writer_buffered(stream);

    writer << std::vector<std::string>{
        "time", "entity_id", "entity_type", "node_id", "node_name", "node_type", "event_type"};

    for (const auto& row : bundle.event_rows)
    {
        writer << std::make_tuple(
            format_fixed(row.time, TIME_PRECISION),
            row.entity_id,
            row.entity_type,
            row.node_id,
            row.node_name,
            row.node_type,
            row.event_type);
    }
}

void Reporter::write_resource_timeline(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix)
{
    auto stream = open_csv_file(csv_path(output_directory, "resource_timeline", file_suffix));
    auto writer = csv::make_csv_writer_buffered(stream);

    writer << std::vector<std::string>{
        "time", "resource_id", "resource_name", "change_type", "in_use", "available", "queue_length", "entity_id", "task_id"};

    for (const auto& row : bundle.resource_timeline_rows)
    {
        writer << std::make_tuple(
            format_fixed(row.time, TIME_PRECISION),
            row.resource_id,
            row.resource_name,
            row.change_type,
            row.in_use,
            row.available,
            row.queue_length,
            row.entity_id,
            row.task_id);
    }
}

void Reporter::write_resource_summary(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix)
{
    auto stream = open_csv_file(csv_path(output_directory, "resource_summary", file_suffix));
    auto writer = csv::make_csv_writer_buffered(stream);

    writer << std::vector<std::string>{
        "resource_id", "resource_name", "capacity", "busy_time", "idle_time", "utilization", "max_queue_length", "average_wait_time", "allocation_count", "simulation_horizon"};

    for (const auto& row : bundle.resource_summary_rows)
    {
        writer << std::make_tuple(
            row.resource_id,
            row.resource_name,
            row.capacity,
            format_fixed(row.busy_time, TIME_PRECISION),
            format_fixed(row.idle_time, TIME_PRECISION),
            format_fixed(row.utilization, RATIO_PRECISION),
            row.max_queue_length,
            format_fixed(row.average_wait_time, TIME_PRECISION),
            row.allocation_count,
            format_fixed(row.simulation_horizon, TIME_PRECISION));
    }
}

void Reporter::report(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix)
{
    std::filesystem::create_directories(output_directory);

    write_events(output_directory, bundle, file_suffix);
    write_resource_timeline(output_directory, bundle, file_suffix);
    write_resource_summary(output_directory, bundle, file_suffix);
}

} // namespace flux