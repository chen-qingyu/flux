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
    std::string entity_name;
    std::string node_id;
    std::string node_name;
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
    std::string task_id;
    std::string task_name;
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
};

struct TaskTimelineRow
{
    double time{0.0};
    std::string task_id;
    std::string task_name;
    int waiting{0};
    int running{0};
    int completed{0};
};

struct TaskSummaryRow
{
    std::string task_id;
    std::string task_name;
    std::size_t arrival_count{0};
    std::size_t start_count{0};
    double busy_time{0.0};
    double busy_rate{0.0};
    double queue_total_time{0.0};
    double queue_average_time{0.0};
    double queue_max_time{0.0};
    double queue_min_time{0.0};
    double process_total_time{0.0};
    double process_average_time{0.0};
    double process_max_time{0.0};
    double process_min_time{0.0};
};

struct ReportBundle
{
    std::vector<EventLogRow> event_rows;
    std::vector<ResourceTimelineRow> resource_timeline_rows;
    std::vector<ResourceSummaryRow> resource_summary_rows;
    std::vector<TaskSummaryRow> task_summary_rows;
    std::vector<TaskTimelineRow> task_timeline_rows;
};

class Reporter
{
public:
    static void report(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix = "");

private:
    static void write_entity_events(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix);
    static void write_resource_timeline(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix);
    static void write_resource_summary(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix);
    static void write_task_summary(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix);
    static void write_task_timeline(const std::filesystem::path& output_directory, const ReportBundle& bundle, const std::string& file_suffix);
};

} // namespace flux