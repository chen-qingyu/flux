#include <catch2/catch_test_macros.hpp>

#include "test_support.hpp"

TEST_CASE("AND gateway golden CSV stays stable", "[golden][and]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "tests" / "and_minimal.bpmn", "and");
}

TEST_CASE("Any-resource golden CSV stays stable", "[golden][any]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "tests" / "any_resource_minimal.bpmn", "any");
}

TEST_CASE("All-resource golden CSV stays stable", "[golden][all]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "tests" / "all_resources_minimal.bpmn", "all");
}

TEST_CASE("FIFO queue golden CSV stays stable", "[golden][fifo]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "tests" / "fifo_queue_minimal.bpmn", "fifo");
}

TEST_CASE("Partial-release oldest-feasible golden CSV stays stable", "[golden][same-timestamp]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "tests" / "same_timestamp_partial_release.bpmn", "same_timestamp_partial_release");
}

TEST_CASE("Joint-release oldest-feasible golden CSV stays stable", "[golden][same-timestamp]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "tests" / "same_timestamp_joint_release.bpmn", "same_timestamp_joint_release");
}