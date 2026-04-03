#include <algorithm>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "test_support.hpp"

TEST_CASE("Any-resource strategy allocates one deterministic resource", "[runtime][any]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "any_resource_minimal.bpmn");

    const auto task_starts = flux::test_support::select_events(result, "task_start");
    REQUIRE(task_starts.size() == 1);

    REQUIRE(result.reports.resource_timeline_rows.size() == 2);
    REQUIRE(result.reports.resource_timeline_rows[0].change_type == "allocate");
    REQUIRE(result.reports.resource_timeline_rows[0].resource_name == "柜员");
    REQUIRE(result.reports.resource_timeline_rows[0].entity_id == "ANY发生器_ticket_0");
    REQUIRE(result.reports.resource_timeline_rows[0].task_id == "Task_service");
    REQUIRE(result.reports.resource_timeline_rows[1].change_type == "release");
    REQUIRE(result.reports.resource_timeline_rows[1].resource_name == "柜员");
    REQUIRE(result.reports.resource_summary_rows.size() == 2);
    REQUIRE(result.reports.resource_summary_rows[0].allocation_count == 1);
    REQUIRE(result.reports.resource_summary_rows[1].allocation_count == 0);
}

TEST_CASE("All-resource strategy allocates every associated resource", "[runtime][all]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "all_resources_minimal.bpmn");

    const auto task_starts = flux::test_support::select_events(result, "task_start");
    REQUIRE(task_starts.size() == 1);

    REQUIRE(result.reports.resource_timeline_rows.size() == 4);
    REQUIRE(result.reports.resource_timeline_rows[0].change_type == "allocate");
    REQUIRE(result.reports.resource_timeline_rows[1].change_type == "allocate");
    REQUIRE(result.reports.resource_timeline_rows[0].time == 0.0);
    REQUIRE(result.reports.resource_timeline_rows[1].time == 0.0);
    REQUIRE(result.reports.resource_timeline_rows[0].entity_id == "ALL发生器_case_0");
    REQUIRE(result.reports.resource_timeline_rows[1].entity_id == "ALL发生器_case_0");
    REQUIRE(result.reports.resource_summary_rows.size() == 2);
    REQUIRE(result.reports.resource_summary_rows[0].allocation_count == 1);
    REQUIRE(result.reports.resource_summary_rows[1].allocation_count == 1);
}

TEST_CASE("FIFO queue starts entities in arrival order", "[runtime][fifo]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "fifo_queue_minimal.bpmn");

    const auto task_starts = flux::test_support::select_events(result, "task_start");
    REQUIRE(task_starts.size() == 3);
    REQUIRE(task_starts[0].entity_id == "FIFO发生器_customer_0");
    REQUIRE(task_starts[1].entity_id == "FIFO发生器_customer_1");
    REQUIRE(task_starts[2].entity_id == "FIFO发生器_customer_2");

    const auto task_arrivals = flux::test_support::select_events(result, "task_arrive");
    REQUIRE(task_arrivals.size() == 3);
    REQUIRE(task_starts[1].time - task_arrivals[1].time == 3.0);
    REQUIRE(task_starts[2].time - task_arrivals[2].time == 6.0);

    REQUIRE(result.reports.resource_summary_rows.size() == 1);
    REQUIRE(result.reports.resource_summary_rows[0].max_queue_length == 2);
    REQUIRE(result.reports.resource_summary_rows[0].average_wait_time == 3.0);
}

TEST_CASE("Older infeasible request does not block younger feasible request", "[runtime][same-timestamp]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "same_timestamp_partial_release.bpmn");

    REQUIRE(flux::test_support::require_event_time(result, "task_start", "Task_need_r1") == 4.0);
    REQUIRE(flux::test_support::require_event_time(result, "task_start", "Task_need_all") == 6.0);
}

TEST_CASE("Oldest feasible request wins when resources free at the same timestamp", "[runtime][same-timestamp]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "same_timestamp_joint_release.bpmn");

    REQUIRE(flux::test_support::require_event_time(result, "task_start", "Task_need_all") == 4.0);
    REQUIRE(flux::test_support::require_event_time(result, "task_start", "Task_need_r1") == 5.0);
}

TEST_CASE("Weighted splitter routes entities across outgoing branches", "[runtime][splitter]")
{
    std::size_t branch_1_count = 0;
    std::size_t branch_2_count = 0;
    std::size_t branch_3_count = 0;
    std::size_t total_task_starts = 0;

    const auto count_task_starts = [](const auto& task_starts, const std::string& node_id)
    {
        return std::count_if(task_starts.begin(), task_starts.end(), [&](const auto& row)
                             { return row.node_id == node_id; });
    };

    constexpr std::uint64_t run_count = 64;
    for (std::uint64_t seed = 1; seed <= run_count; ++seed)
    {
        const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "splitter.bpmn", seed);
        const auto task_starts = flux::test_support::select_events(result, "task_start");

        total_task_starts += task_starts.size();
        branch_1_count += count_task_starts(task_starts, "Activity_1");
        branch_2_count += count_task_starts(task_starts, "Activity_2");
        branch_3_count += count_task_starts(task_starts, "Activity_3");
    }

    REQUIRE(total_task_starts == run_count * 100);
    REQUIRE(branch_1_count + branch_2_count + branch_3_count == total_task_starts);

    const auto branch_1_ratio = static_cast<double>(branch_1_count) / static_cast<double>(total_task_starts);
    const auto branch_2_ratio = static_cast<double>(branch_2_count) / static_cast<double>(total_task_starts);
    const auto branch_3_ratio = static_cast<double>(branch_3_count) / static_cast<double>(total_task_starts);

    REQUIRE(std::abs(branch_1_ratio - (1.0 / 6.0)) < 0.03);
    REQUIRE(std::abs(branch_2_ratio - (2.0 / 6.0)) < 0.03);
    REQUIRE(std::abs(branch_3_ratio - (3.0 / 6.0)) < 0.03);
    REQUIRE(branch_1_ratio < branch_2_ratio);
    REQUIRE(branch_2_ratio < branch_3_ratio);
}

TEST_CASE("Transport task accumulates total distance on completion", "[runtime][transport]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "transport_minimal.bpmn");

    const auto task_finishes = flux::test_support::select_events(result, "task_finish");
    REQUIRE(task_finishes.size() == 3);
    REQUIRE(result.generated_entities == 3);
    REQUIRE(result.completed_entities == 3);
    REQUIRE(std::abs(result.total_transport_distance - 61.2) < 1e-9);
}
