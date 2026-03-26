#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace flux
{

struct EventLogRow
{
    double time{0.0};
    std::string entity_id;
    std::string entity_type;
    std::string node_id;
    std::string node_name;
    std::string node_type;
    std::string event_type;
};

struct ResourceTimelineRow
{
    double time{0.0};
    std::string resource_id;
    std::string resource_name;
    std::string change_type;
    int in_use{0};
    int available{0};
    int queue_length{0};
    std::string entity_id;
    std::string task_id;
};

struct ResourceSummaryRow
{
    std::string resource_id;
    std::string resource_name;
    int capacity{0};
    double busy_time{0.0};
    double idle_time{0.0};
    double utilization{0.0};
    int max_queue_length{0};
    double average_wait_time{0.0};
    std::size_t allocation_count{0};
    double simulation_horizon{0.0};
};

struct ReportBundle
{
    std::vector<EventLogRow> event_rows;
    std::vector<ResourceTimelineRow> resource_timeline_rows;
    std::vector<ResourceSummaryRow> resource_summary_rows;
};

void write_reports(const std::filesystem::path& output_directory, const ReportBundle& bundle);

} // namespace flux