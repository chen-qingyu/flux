#include "reporter.hpp"

#include <algorithm>
#include <chrono>
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

std::ofstream open_csv(const std::filesystem::path& dir, const std::string& report_name, const std::string& input_file, const long long& ts)
{
    auto path = input_file.empty() ? dir / (report_name + ".csv") : dir / fmt::format("{}-{}-{}.csv", input_file, report_name, ts);
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("Failed to open " + path.filename().string() + " for writing.");
    }
    return stream;
}

} // namespace

void Reporter::write_entity_events(const std::filesystem::path& output_dir, const ReportBundle& bundle, const std::string& input_file, const long long& ts)
{
    auto stream = open_csv(output_dir, "entity_events", input_file, ts);
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

void Reporter::write_resource_timeline(const std::filesystem::path& output_dir, const ReportBundle& bundle, const std::string& input_file, const long long& ts)
{
    auto stream = open_csv(output_dir, "resource_timeline", input_file, ts);
    auto writer = csv::make_csv_writer_buffered(stream);

    writer << std::vector<std::string>{
        "time", "resource_id", "resource_name", "change_type", "in_use", "available", "queue_length", "task_id", "task_name"};

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
            row.task_id,
            row.task_name);
    }
}

void Reporter::write_resource_summary(const std::filesystem::path& output_dir, const ReportBundle& bundle, const std::string& input_file, const long long& ts)
{
    auto stream = open_csv(output_dir, "resource_summary", input_file, ts);
    auto writer = csv::make_csv_writer_buffered(stream);
    auto rows = bundle.resource_summary_rows;
    std::stable_sort(rows.begin(), rows.end(), [](const auto& left, const auto& right)
                     { return std::tie(left.resource_name, left.resource_id) < std::tie(right.resource_name, right.resource_id); });

    writer << std::vector<std::string>{
        "resource_name", "resource_id", "capacity", "busy_time", "idle_time", "utilization", "max_queue_length", "average_wait_time", "allocation_count"};

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
            row.allocation_count);
    }
}

void Reporter::write_task_summary(const std::filesystem::path& output_dir, const ReportBundle& bundle, const std::string& input_file, const long long& ts)
{
    auto stream = open_csv(output_dir, "task_summary", input_file, ts);
    auto writer = csv::make_csv_writer_buffered(stream);
    auto rows = bundle.task_summary_rows;
    std::stable_sort(rows.begin(), rows.end(), [](const auto& left, const auto& right)
                     { return std::tie(left.task_name, left.task_id) < std::tie(right.task_name, right.task_id); });

    writer << std::vector<std::string>{
        "task_name", "task_id", "arrival_count", "start_count", "busy_time", "busy_rate",
        "queue_total_time", "queue_average_time", "queue_max_time", "queue_min_time",
        "process_total_time", "process_average_time", "process_max_time", "process_min_time"};

    for (const auto& row : rows)
    {
        writer << std::make_tuple(
            row.task_name,
            row.task_id,
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

void Reporter::write_task_timeline(const std::filesystem::path& output_dir, const ReportBundle& bundle, const std::string& input_file, const long long& ts)
{
    auto stream = open_csv(output_dir, "task_timeline", input_file, ts);
    auto writer = csv::make_csv_writer_buffered(stream);

    writer << std::vector<std::string>{
        "time", "task_id", "task_name", "waiting", "running", "completed"};

    for (const auto& row : bundle.task_timeline_rows)
    {
        writer << std::make_tuple(
            format_fixed(row.time, TIME_PRECISION),
            row.task_id,
            row.task_name,
            row.waiting,
            row.running,
            row.completed);
    }
}

void Reporter::report(const std::filesystem::path& output_dir, const ReportBundle& bundle, const std::string& input_file)
{
    std::filesystem::create_directories(output_dir);

    const auto now = std::chrono::system_clock::now();
    const auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    write_entity_events(output_dir, bundle, input_file, ts);
    write_resource_timeline(output_dir, bundle, input_file, ts);
    write_resource_summary(output_dir, bundle, input_file, ts);
    write_task_summary(output_dir, bundle, input_file, ts);
    write_task_timeline(output_dir, bundle, input_file, ts);
}

} // namespace flux
