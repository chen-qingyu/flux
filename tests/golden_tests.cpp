#include <catch2/catch_test_macros.hpp>

#include "test_support.hpp"

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

TEST_CASE("Arbitration golden CSV stays stable", "[golden][same-timestamp]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "tests" / "arbitration.bpmn", "arbitration");
}

TEST_CASE("Lifecycle golden CSV stays stable", "[golden][resource-lifecycle]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "tests" / "lifecycle.bpmn", "lifecycle");
}

TEST_CASE("Transport golden CSV stays stable", "[golden][transport]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "tests" / "transport_minimal.bpmn", "transport");
}

TEST_CASE("Combine split golden CSV stays stable", "[golden][combine-split]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "tests" / "combine_split_minimal.bpmn", "combine_split");
}