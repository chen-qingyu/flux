#include "csv_reporting.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace flux
{
namespace
{

std::string escape_csv(const std::string& value)
{
    if (value.find_first_of(",\"\n\r") == std::string::npos)
    {
        return value;
    }

    std::string escaped;
    escaped.reserve(value.size() + 4);
    escaped.push_back('"');
    for (const char ch : value)
    {
        if (ch == '"')
        {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

void write_line(std::ofstream& stream, const std::vector<std::string>& columns)
{
    for (std::size_t index = 0; index < columns.size(); ++index)
    {
        if (index > 0)
        {
            stream << ',';
        }
        stream << escape_csv(columns[index]);
    }
    stream << '\n';
}

} // namespace

std::string format_time(double value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    auto text = stream.str();
    while (!text.empty() && text.back() == '0')
    {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.')
    {
        text.pop_back();
    }
    return text.empty() ? "0" : text;
}

void write_reports(const std::filesystem::path& output_directory, const ReportBundle& bundle)
{
    std::filesystem::create_directories(output_directory);

    {
        std::ofstream stream(output_directory / "events.csv", std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("Failed to open events.csv for writing.");
        }

        write_line(stream, {"time", "entity_id", "entity_type", "node_id", "node_name", "node_type", "event_type"});
        for (const auto& row : bundle.event_rows)
        {
            write_line(stream, {
                                   format_time(row.time),
                                   row.entity_id,
                                   row.entity_type,
                                   row.node_id,
                                   row.node_name,
                                   row.node_type,
                                   row.event_type,
                               });
        }
    }

    {
        std::ofstream stream(output_directory / "resource_timeline.csv", std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("Failed to open resource_timeline.csv for writing.");
        }

        write_line(stream, {"time", "resource_id", "resource_name", "change_type", "in_use", "available", "queue_length", "entity_id", "task_id"});
        for (const auto& row : bundle.resource_timeline_rows)
        {
            write_line(stream, {
                                   format_time(row.time),
                                   row.resource_id,
                                   row.resource_name,
                                   row.change_type,
                                   std::to_string(row.in_use),
                                   std::to_string(row.available),
                                   std::to_string(row.queue_length),
                                   row.entity_id,
                                   row.task_id,
                               });
        }
    }

    {
        std::ofstream stream(output_directory / "resource_summary.csv", std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("Failed to open resource_summary.csv for writing.");
        }

        write_line(stream, {"resource_id", "resource_name", "capacity", "busy_time", "idle_time", "utilization", "max_queue_length", "average_wait_time", "allocation_count", "simulation_horizon"});
        for (const auto& row : bundle.resource_summary_rows)
        {
            write_line(stream, {
                                   row.resource_id,
                                   row.resource_name,
                                   std::to_string(row.capacity),
                                   format_time(row.busy_time),
                                   format_time(row.idle_time),
                                   format_time(row.utilization),
                                   std::to_string(row.max_queue_length),
                                   format_time(row.average_wait_time),
                                   std::to_string(row.allocation_count),
                                   format_time(row.simulation_horizon),
                               });
        }
    }
}

} // namespace flux