#include <catch2/catch_test_macros.hpp>

#include "test_support.hpp"

TEST_CASE("XOR gateway golden CSV stays stable", "[golden][xor]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "xor_minimal.bpmn", "xor");
}

TEST_CASE("AND gateway golden CSV stays stable", "[golden][and]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "and_minimal.bpmn", "and");
}

TEST_CASE("Any-resource golden CSV stays stable", "[golden][any]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "any_resource_minimal.bpmn", "any");
}

TEST_CASE("All-resource golden CSV stays stable", "[golden][all]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "all_resources_minimal.bpmn", "all");
}

TEST_CASE("FIFO queue golden CSV stays stable", "[golden][fifo]")
{
    flux::test_support::require_report_matches(std::filesystem::path("data") / "fifo_queue_minimal.bpmn", "fifo");
}