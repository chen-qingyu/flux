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