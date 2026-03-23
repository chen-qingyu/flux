#include <Catch2/catch_test_macros.hpp>

#include "test_support.hpp"

TEST_CASE("Any-resource strategy allocates one deterministic resource", "[runtime][any]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "any_resource_minimal.bpmn");

    const auto task_starts = flux::test_support::select_events(result, "task_start");
    REQUIRE(task_starts.size() == 1);
    REQUIRE(task_starts.front().resource_snapshot.find("柜员") != std::string::npos);
    REQUIRE(task_starts.front().resource_snapshot.find("自助机") != std::string::npos);
    REQUIRE(task_starts.front().details_json.find("柜员") != std::string::npos);

    REQUIRE(result.reports.resource_timeline_rows.size() == 2);
    REQUIRE(result.reports.resource_timeline_rows[0].resource_name == "柜员");
    REQUIRE(result.reports.resource_summary_rows.size() == 2);
    REQUIRE(result.reports.resource_summary_rows[0].allocation_count == 1);
    REQUIRE(result.reports.resource_summary_rows[1].allocation_count == 0);
}

TEST_CASE("All-resource strategy allocates every associated resource", "[runtime][all]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "all_resources_minimal.bpmn");

    const auto task_starts = flux::test_support::select_events(result, "task_start");
    REQUIRE(task_starts.size() == 1);
    REQUIRE(task_starts.front().details_json.find("柜员") != std::string::npos);
    REQUIRE(task_starts.front().details_json.find("电脑") != std::string::npos);

    REQUIRE(result.reports.resource_timeline_rows.size() == 4);
    REQUIRE(result.reports.resource_summary_rows.size() == 2);
    REQUIRE(result.reports.resource_summary_rows[0].allocation_count == 1);
    REQUIRE(result.reports.resource_summary_rows[1].allocation_count == 1);
}

TEST_CASE("FIFO queue starts entities in arrival order", "[runtime][fifo]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "fifo_queue_minimal.bpmn");

    const auto task_starts = flux::test_support::select_events(result, "task_start");
    REQUIRE(task_starts.size() == 3);
    REQUIRE(task_starts[0].entity_id == "FIFO发生器_customer_0");
    REQUIRE(task_starts[1].entity_id == "FIFO发生器_customer_1");
    REQUIRE(task_starts[2].entity_id == "FIFO发生器_customer_2");
    REQUIRE(task_starts[1].details_json.find("\"wait_time\":3") != std::string::npos);
    REQUIRE(task_starts[2].details_json.find("\"wait_time\":6") != std::string::npos);

    REQUIRE(result.reports.resource_summary_rows.size() == 1);
    REQUIRE(result.reports.resource_summary_rows[0].max_queue_length == 2);
    REQUIRE(result.reports.resource_summary_rows[0].average_wait_time == 3.0);
}