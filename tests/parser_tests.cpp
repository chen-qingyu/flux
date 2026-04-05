#include <catch2/catch_test_macros.hpp>

#include "test_support.hpp"

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

TEST_CASE("Parser reads weighted splitter model", "[parser][splitter]")
{
    const auto model = flux::test_support::parse_model(std::filesystem::path("data") / "tests" / "splitter.bpmn");

    const auto& gateway = flux::node(model, "Gateway_splitter");
    REQUIRE(gateway.gateway_criteria == flux::GatewayCriteria::ByWeight);
    REQUIRE(model.outgoing_flow_ids.at("Gateway_splitter").size() == 3);

    const auto& flow_1 = flux::flow(model, "Flow_07g491b");
    const auto& flow_2 = flux::flow(model, "Flow_1ee3144");
    const auto& flow_3 = flux::flow(model, "Flow_0aoi10x");

    REQUIRE(flow_1.name == "1");
    REQUIRE(flow_1.weight == 1.0);
    REQUIRE(flow_2.weight == 2.0);
    REQUIRE(flow_3.weight == 3.0);
}

TEST_CASE("Parser reads transport task model", "[parser][transport]")
{
    const auto model = flux::test_support::parse_model(std::filesystem::path("data") / "tests" / "transport_minimal.bpmn");

    const auto& task = flux::node(model, "Task_transport");
    REQUIRE(task.task.has_value());
    REQUIRE(task.task->type == flux::TaskType::Transport);
    REQUIRE(task.task->distance == 20.4);
    REQUIRE(task.task->duration_distribution.type == flux::DistributionType::Static);
    REQUIRE(task.task->duration_distribution.first == 2.0);
}

TEST_CASE("Parser reads acquire and release resource tasks", "[parser][resource-lifecycle]")
{
    const auto model = flux::test_support::parse_model(std::filesystem::path("data") / "tests" / "resource_binding.bpmn");

    const auto& acquire = flux::node(model, "Activity_acquire");
    const auto& release = flux::node(model, "Activity_release");

    REQUIRE(acquire.task.has_value());
    REQUIRE(acquire.task->type == flux::TaskType::AcquireResource);
    REQUIRE(acquire.task->resource_strategy == flux::ResourceStrategy::All);
    REQUIRE(model.task_resources.at("Activity_acquire").size() == 1);
    REQUIRE(model.task_resources.at("Activity_acquire").front() == "DataStoreReference_resource");

    REQUIRE(release.task.has_value());
    REQUIRE(release.task->type == flux::TaskType::ReleaseResource);
    REQUIRE(!release.task->resource_strategy.has_value());
    REQUIRE(model.task_resources.at("Activity_release").size() == 1);
    REQUIRE(model.task_resources.at("Activity_release").front() == "DataStoreReference_resource");
}
