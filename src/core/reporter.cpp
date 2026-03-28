#include "reporter.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

namespace flux
{
namespace
{

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

void write_events(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix)
{
    auto stream = open_csv_file(csv_path(output_directory, "events", file_suffix));

    stream << "time,entity_id,entity_type,node_id,node_name,node_type,event_type\n";
    for (const auto& row : bundle.event_rows)
    {
        stream << fmt::format(
            "{:.3f},{},{},{},{},{},{}\n",
            row.time,
            row.entity_id,
            row.entity_type,
            row.node_id,
            row.node_name,
            row.node_type,
            row.event_type);
    }
}

void write_resource_timeline(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix)
{
    auto stream = open_csv_file(csv_path(output_directory, "resource_timeline", file_suffix));

    stream << "time,resource_id,resource_name,change_type,in_use,available,queue_length,entity_id,task_id\n";
    for (const auto& row : bundle.resource_timeline_rows)
    {
        stream << fmt::format(
            "{:.3f},{},{},{},{},{},{},{},{}\n",
            row.time,
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

void write_resource_summary(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix)
{
    auto stream = open_csv_file(csv_path(output_directory, "resource_summary", file_suffix));

    stream << "resource_id,resource_name,capacity,busy_time,idle_time,utilization,max_queue_length,average_wait_time,allocation_count,simulation_horizon\n";
    for (const auto& row : bundle.resource_summary_rows)
    {
        stream << fmt::format(
            "{},{},{},{:.3f},{:.3f},{:.3f},{},{:.3f},{},{:.3f}\n",
            row.resource_id,
            row.resource_name,
            row.capacity,
            row.busy_time,
            row.idle_time,
            row.utilization,
            row.max_queue_length,
            row.average_wait_time,
            row.allocation_count,
            row.simulation_horizon);
    }
}

} // namespace

void write_reports(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix)
{
    std::filesystem::create_directories(output_directory);

    write_events(output_directory, bundle, file_suffix);
    write_resource_timeline(output_directory, bundle, file_suffix);
    write_resource_summary(output_directory, bundle, file_suffix);
}

} // namespace flux