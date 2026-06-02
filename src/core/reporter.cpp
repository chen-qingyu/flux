#include "reporter.hpp"

#include <algorithm>
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

void Reporter::write_entity_events(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix)
{
    auto stream = open_csv_file(csv_path(output_directory, "entity_events", file_suffix));
    auto writer = csv::make_csv_writer_buffered(stream);

    writer << std::vector<std::string>{
        "time", "entity_id", "entity_name", "node_id", "node_name", "event_type"};

    for (const auto& row : bundle.event_rows)
    {
        writer << std::make_tuple(
            format_fixed(row.time, TIME_PRECISION),
            row.entity_id,
            row.entity_name,
            row.node_id,
            row.node_name,
            row.event_type);
    }
}

void Reporter::write_resource_timeline(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix)
{
    auto stream = open_csv_file(csv_path(output_directory, "resource_timeline", file_suffix));
    auto writer = csv::make_csv_writer_buffered(stream);

    writer << std::vector<std::string>{
        "time", "resource_id", "resource_name", "change_type", "in_use", "available", "queue_length", "task_id"};

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
            row.task_id);
    }
}

void Reporter::write_resource_summary(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix)
{
    auto stream = open_csv_file(csv_path(output_directory, "resource_summary", file_suffix));
    auto writer = csv::make_csv_writer_buffered(stream);
    auto rows = bundle.resource_summary_rows;
    std::stable_sort(rows.begin(), rows.end(), [](const auto& left, const auto& right)
                     { return std::tie(left.resource_name, left.resource_id) < std::tie(right.resource_name, right.resource_id); });

    writer << std::vector<std::string>{
        "resource_name", "resource_id", "capacity", "busy_time", "idle_time", "utilization", "max_queue_length", "average_wait_time", "allocation_count", "simulation_horizon"};

    for (const auto& row : rows)
    {
        writer << std::make_tuple(
            row.resource_name,
            row.resource_id,
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

void Reporter::write_activity_summary(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix)
{
    auto stream = open_csv_file(csv_path(output_directory, "activity_summary", file_suffix));
    auto writer = csv::make_csv_writer_buffered(stream);
    auto rows = bundle.activity_summary_rows;
    std::stable_sort(rows.begin(), rows.end(), [](const auto& left, const auto& right)
                     { return std::tie(left.activity_name, left.activity_id) < std::tie(right.activity_name, right.activity_id); });

    writer << std::vector<std::string>{
        "activity_name", "activity_id", "arrival_count", "start_count", "busy_time", "busy_rate",
        "queue_total_time", "queue_average_time", "queue_max_time", "queue_min_time",
        "process_total_time", "process_average_time", "process_max_time", "process_min_time"};

    for (const auto& row : rows)
    {
        writer << std::make_tuple(
            row.activity_name,
            row.activity_id,
            row.arrival_count,
            row.start_count,
            format_fixed(row.busy_time, TIME_PRECISION),
            format_fixed(row.busy_rate, RATIO_PRECISION),
            format_fixed(row.queue_total_time, TIME_PRECISION),
            format_fixed(row.queue_average_time, TIME_PRECISION),
            format_fixed(row.queue_max_time, TIME_PRECISION),
            format_fixed(row.queue_min_time, TIME_PRECISION),
            format_fixed(row.process_total_time, TIME_PRECISION),
            format_fixed(row.process_average_time, TIME_PRECISION),
            format_fixed(row.process_max_time, TIME_PRECISION),
            format_fixed(row.process_min_time, TIME_PRECISION));
    }
}

void Reporter::report(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix)
{
    std::filesystem::create_directories(output_directory);

    write_entity_events(output_directory, bundle, file_suffix);
    write_resource_timeline(output_directory, bundle, file_suffix);
    write_resource_summary(output_directory, bundle, file_suffix);
    write_activity_summary(output_directory, bundle, file_suffix);
}

} // namespace flux
