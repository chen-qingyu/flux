#include <algorithm>
#include <chrono>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "test_support.hpp"

TEST_CASE("Any-resource strategy allocates one deterministic resource", "[runtime][any]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "any_resource.bpmn");

    const auto task_starts = flux::test_support::select_events(result, "task_start");
    REQUIRE(task_starts.size() == 1);

    REQUIRE(result.reports.resource_timeline_rows.size() == 2);
    REQUIRE(result.reports.resource_timeline_rows[0].change_type == "allocate");
    REQUIRE(result.reports.resource_timeline_rows[0].resource_name == "柜员");
    REQUIRE(result.reports.resource_timeline_rows[0].entity_id == "Event_start_ticket_0");
    REQUIRE(result.reports.resource_timeline_rows[0].task_id == "Task_service");
    REQUIRE(result.reports.resource_timeline_rows[1].change_type == "release");
    REQUIRE(result.reports.resource_timeline_rows[1].resource_name == "柜员");
    REQUIRE(result.reports.resource_summary_rows.size() == 2);
    REQUIRE(result.reports.resource_summary_rows[0].allocation_count == 1);
    REQUIRE(result.reports.resource_summary_rows[1].allocation_count == 0);
}

TEST_CASE("All-resource strategy allocates every associated resource", "[runtime][all]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "all_resources.bpmn");

    const auto task_starts = flux::test_support::select_events(result, "task_start");
    REQUIRE(task_starts.size() == 1);

    REQUIRE(result.reports.resource_timeline_rows.size() == 4);
    REQUIRE(result.reports.resource_timeline_rows[0].change_type == "allocate");
    REQUIRE(result.reports.resource_timeline_rows[1].change_type == "allocate");
    REQUIRE(result.reports.resource_timeline_rows[0].time == 0.0);
    REQUIRE(result.reports.resource_timeline_rows[1].time == 0.0);
    REQUIRE(result.reports.resource_timeline_rows[0].entity_id == "Event_start_case_0");
    REQUIRE(result.reports.resource_timeline_rows[1].entity_id == "Event_start_case_0");
    REQUIRE(result.reports.resource_summary_rows.size() == 2);
    REQUIRE(result.reports.resource_summary_rows[0].allocation_count == 1);
    REQUIRE(result.reports.resource_summary_rows[1].allocation_count == 1);
}

TEST_CASE("FIFO queue starts entities in arrival order", "[runtime][fifo]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "fifo_queue.bpmn");

    const auto task_starts = flux::test_support::select_events(result, "task_start");
    REQUIRE(task_starts.size() == 3);
    REQUIRE(task_starts[0].entity_id == "Event_start_customer_0");
    REQUIRE(task_starts[1].entity_id == "Event_start_customer_1");
    REQUIRE(task_starts[2].entity_id == "Event_start_customer_2");

    const auto task_arrivals = flux::test_support::select_events(result, "task_arrive");
    REQUIRE(task_arrivals.size() == 3);
    REQUIRE(task_starts[1].time - task_arrivals[1].time == 3.0);
    REQUIRE(task_starts[2].time - task_arrivals[2].time == 6.0);

    REQUIRE(result.reports.resource_summary_rows.size() == 1);
    REQUIRE(result.reports.resource_summary_rows[0].max_queue_length == 2);
    REQUIRE(result.reports.resource_summary_rows[0].average_wait_time == 3.0);
}

TEST_CASE("Arbitration model covers oldest-feasible waiting rules", "[runtime][same-timestamp]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "arbitration.bpmn");

    REQUIRE(flux::test_support::require_event_time(result, "task_start", "Task_need_partial_p1") == 4.0);
    REQUIRE(flux::test_support::require_event_time(result, "task_start", "Task_need_partial_all") == 6.0);
    REQUIRE(flux::test_support::require_event_time(result, "task_start", "Task_need_joint_all") == 4.0);
    REQUIRE(flux::test_support::require_event_time(result, "task_start", "Task_need_joint_j1") == 5.0);
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
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "transport.bpmn");

    const auto task_finishes = flux::test_support::select_events(result, "task_finish");
    REQUIRE(task_finishes.size() == 3);
    REQUIRE(result.generated_entities == 3);
    REQUIRE(result.completed_entities == 3);
    REQUIRE(std::abs(result.total_transport_distance - 61.2) < 1e-9);
}

TEST_CASE("Lifecycle model releases bound and remaining held resources correctly", "[runtime][resource-lifecycle]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "lifecycle.bpmn");

    REQUIRE(result.reports.resource_timeline_rows.size() == 8);
    REQUIRE(result.reports.resource_timeline_rows[0].change_type == "allocate");
    REQUIRE(result.reports.resource_timeline_rows[1].change_type == "allocate");
    REQUIRE(result.reports.resource_timeline_rows[2].change_type == "release");
    REQUIRE(result.reports.resource_timeline_rows[3].change_type == "release");
    REQUIRE(result.reports.resource_timeline_rows[4].change_type == "allocate");
    REQUIRE(result.reports.resource_timeline_rows[5].change_type == "allocate");
    REQUIRE(result.reports.resource_timeline_rows[6].change_type == "release");
    REQUIRE(result.reports.resource_timeline_rows[7].change_type == "release");
    REQUIRE(result.reports.resource_timeline_rows[2].resource_name == "叉车");
    REQUIRE(result.reports.resource_timeline_rows[2].task_id == "Task_release_bound");
    REQUIRE(result.reports.resource_timeline_rows[2].time == 3.0);
    REQUIRE(result.reports.resource_timeline_rows[3].resource_name == "司机");
    REQUIRE(result.reports.resource_timeline_rows[3].task_id == "Task_release_all_remaining");
    REQUIRE(result.reports.resource_timeline_rows[3].time == 5.0);
    REQUIRE(result.reports.resource_timeline_rows[6].task_id == "Task_release_all");
    REQUIRE(result.reports.resource_timeline_rows[7].task_id == "Task_release_all");
    REQUIRE(result.reports.resource_timeline_rows[6].time == 13.0);
    REQUIRE(result.reports.resource_timeline_rows[7].time == 13.0);

    const auto forklift_summary = std::find_if(result.reports.resource_summary_rows.begin(), result.reports.resource_summary_rows.end(), [](const auto& row)
                                               { return row.resource_name == "叉车"; });
    const auto driver_summary = std::find_if(result.reports.resource_summary_rows.begin(), result.reports.resource_summary_rows.end(), [](const auto& row)
                                             { return row.resource_name == "司机"; });

    REQUIRE(forklift_summary != result.reports.resource_summary_rows.end());
    REQUIRE(driver_summary != result.reports.resource_summary_rows.end());
    REQUIRE(forklift_summary->allocation_count == 2);
    REQUIRE(driver_summary->allocation_count == 2);
    REQUIRE(forklift_summary->busy_time == 6.0);
    REQUIRE(driver_summary->busy_time == 8.0);
}

TEST_CASE("Floating-point ratios work across combine delay split pipeline", "[runtime][combine][split][ratio-float]")
{
    const auto result = flux::test_support::run_model(std::filesystem::path("data") / "tests" / "float_ratio_pipeline.bpmn");
    const auto task_starts = flux::test_support::select_events(result, "task_start");
    const auto exits = flux::test_support::select_events(result, "entity_exit");
    const auto combine_starts = std::count_if(task_starts.begin(), task_starts.end(), [](const auto& row)
                                              { return row.node_id == "Activity_combine"; });
    const auto delay_starts = std::count_if(task_starts.begin(), task_starts.end(), [](const auto& row)
                                            { return row.node_id == "Activity_delay"; });
    const auto split_starts = std::count_if(task_starts.begin(), task_starts.end(), [](const auto& row)
                                            { return row.node_id == "Activity_split"; });

    REQUIRE(result.generated_entities == 38);
    REQUIRE(result.completed_entities == 38);
    REQUIRE(combine_starts == 10);
    REQUIRE(delay_starts == 10);
    REQUIRE(split_starts == 10);
    REQUIRE(exits.size() == 38);

    for (const auto& row : task_starts)
    {
        if (row.node_id == "Activity_combine")
        {
            REQUIRE(row.entity_type == "box");
        }
        if (row.node_id == "Activity_delay")
        {
            REQUIRE(row.entity_type == "box");
        }
        if (row.node_id == "Activity_split")
        {
            REQUIRE(row.entity_type == "box");
        }
    }

    for (const auto& row : exits)
    {
        REQUIRE(row.node_id == "Event_end");
        REQUIRE(row.entity_type == "parcel");
    }
}

#ifdef NDEBUG
TEST_CASE("Multisrc runtime stays under three seconds", "[runtime][perf]")
{
    constexpr double threshold_seconds = 3.0;

    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "multi_resources.bpmn");

    const auto started_at = std::chrono::steady_clock::now();
    const auto result = flux::Engine::run(model, 42);
    const auto finished_at = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(finished_at - started_at).count();

    REQUIRE(result.generated_entities == 10000);
    REQUIRE(result.completed_entities == 10000);
    REQUIRE(elapsed < threshold_seconds);
}
#endif
