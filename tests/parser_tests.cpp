#include <catch2/catch_test_macros.hpp>

#include "test_support.hpp"
#include "tools.hpp"

TEST_CASE("Parser reads any-resource task model", "[parser][any]")
{
    const auto model = flux::test_support::parse_model(std::filesystem::path("data") / "tests" / "any_resource_minimal.bpmn");

    const auto& start = flux::node(model, "Event_start");
    const auto& task = flux::node(model, "Task_service");

    REQUIRE(start.generator.has_value());
    REQUIRE(start.generator->entity_type == "ticket");
    REQUIRE(task.task.has_value());
    REQUIRE(task.task->resource_strategy == flux::ResourceStrategy::Any);
    REQUIRE(model.task_resources.at("Task_service").size() == 2);
}

TEST_CASE("Parser reads all-resource task model", "[parser][all]")
{
    const auto model = flux::test_support::parse_model(std::filesystem::path("data") / "tests" / "all_resources_minimal.bpmn");

    const auto& task = flux::node(model, "Task_service");
    REQUIRE(task.task.has_value());
    REQUIRE(task.task->resource_strategy == flux::ResourceStrategy::All);
    REQUIRE(model.task_resources.at("Task_service").size() == 2);
    REQUIRE(model.resources.at("DataStoreReference_clerk").capacity == 1);
    REQUIRE(model.resources.at("DataStoreReference_pc").capacity == 1);
}

TEST_CASE("Parser reads fifo generator count", "[parser][fifo]")
{
    const auto model = flux::test_support::parse_model(std::filesystem::path("data") / "tests" / "fifo_queue_minimal.bpmn");

    const auto& start = flux::node(model, "Event_start");
    REQUIRE(start.generator.has_value());
    REQUIRE(start.generator->entity_count == 3);
    REQUIRE(start.generator->entity_type == "customer");
}
