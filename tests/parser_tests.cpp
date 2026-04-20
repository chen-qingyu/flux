#include <catch2/catch_test_macros.hpp>

#include "test_support.hpp"

TEST_CASE("Parser reads any-resource task model", "[parser][any]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "any_resource_minimal.bpmn");

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
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "all_resources_minimal.bpmn");

    const auto& task = flux::node(model, "Task_service");
    REQUIRE(task.task.has_value());
    REQUIRE(task.task->resource_strategy == flux::ResourceStrategy::All);
    REQUIRE(model.task_resources.at("Task_service").size() == 2);
    REQUIRE(model.resources.at("DataStoreReference_clerk").capacity == 1);
    REQUIRE(model.resources.at("DataStoreReference_pc").capacity == 1);
}

TEST_CASE("Parser reads fifo generator count", "[parser][fifo]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "fifo_queue_minimal.bpmn");

    const auto& start = flux::node(model, "Event_start");
    REQUIRE(start.generator.has_value());
    REQUIRE(start.generator->entity_count == 3);
    REQUIRE(start.generator->entity_type == "customer");
}

TEST_CASE("Parser reads weighted splitter model", "[parser][splitter]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "splitter.bpmn");

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
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "transport_minimal.bpmn");

    const auto& task = flux::node(model, "Task_transport");
    REQUIRE(task.task.has_value());
    REQUIRE(task.task->type == flux::TaskType::Transport);
    REQUIRE(task.task->distance == 20.4);
    REQUIRE(task.task->duration_distribution.type == flux::DistributionType::Static);
    REQUIRE(task.task->duration_distribution.first == 2.0);
}

TEST_CASE("Parser reads resource lifecycle model", "[parser][resource-lifecycle]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "lifecycle.bpmn");

    const auto& acquire_subset = flux::node(model, "Task_acquire_subset");
    const auto& release_bound = flux::node(model, "Task_release_bound");
    const auto& release_all_remaining = flux::node(model, "Task_release_all_remaining");
    const auto& acquire_all = flux::node(model, "Task_acquire_all");
    const auto& release_all = flux::node(model, "Task_release_all");

    REQUIRE(acquire_subset.task.has_value());
    REQUIRE(acquire_subset.task->type == flux::TaskType::AcquireResource);
    REQUIRE(acquire_subset.task->resource_strategy == flux::ResourceStrategy::All);
    REQUIRE(model.task_resources.at("Task_acquire_subset").size() == 2);
    REQUIRE(model.task_resources.at("Task_acquire_subset").front() == "DataStoreReference_driver");
    REQUIRE(model.task_resources.at("Task_acquire_subset").back() == "DataStoreReference_forklift");

    REQUIRE(release_bound.task.has_value());
    REQUIRE(release_bound.task->type == flux::TaskType::ReleaseResource);
    REQUIRE(!release_bound.task->resource_strategy.has_value());
    REQUIRE(model.task_resources.at("Task_release_bound").size() == 1);
    REQUIRE(model.task_resources.at("Task_release_bound").front() == "DataStoreReference_forklift");

    REQUIRE(release_all_remaining.task.has_value());
    REQUIRE(release_all_remaining.task->type == flux::TaskType::ReleaseResource);
    REQUIRE(!release_all_remaining.task->resource_strategy.has_value());
    REQUIRE(model.task_resources.find("Task_release_all_remaining") == model.task_resources.end());

    REQUIRE(acquire_all.task.has_value());
    REQUIRE(acquire_all.task->type == flux::TaskType::AcquireResource);
    REQUIRE(acquire_all.task->resource_strategy == flux::ResourceStrategy::All);
    REQUIRE(model.task_resources.at("Task_acquire_all").size() == 2);

    REQUIRE(release_all.task.has_value());
    REQUIRE(release_all.task->type == flux::TaskType::ReleaseResource);
    REQUIRE(!release_all.task->resource_strategy.has_value());
    REQUIRE(model.task_resources.find("Task_release_all") == model.task_resources.end());
}

TEST_CASE("Parser reads combine and split ratio task model", "[parser][combine-split]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "combine_split_minimal.bpmn");

    const auto& combine = flux::node(model, "Activity_combine");
    const auto& split = flux::node(model, "Activity_split");

    REQUIRE(combine.task.has_value());
    REQUIRE(combine.task->type == flux::TaskType::Combine);
    REQUIRE(combine.task->duration_distribution.type == flux::DistributionType::Static);
    REQUIRE(combine.task->duration_distribution.first == 10.0);
    REQUIRE(combine.task->combine.has_value());
    REQUIRE(combine.task->combine->method == flux::CombineMethod::Ratio);
    REQUIRE(combine.task->combine->ratio == 4);
    REQUIRE(combine.task->combine->entity_type == "truck");

    REQUIRE(split.task.has_value());
    REQUIRE(split.task->type == flux::TaskType::Split);
    REQUIRE(split.task->duration_distribution.type == flux::DistributionType::Static);
    REQUIRE(split.task->duration_distribution.first == 10.0);
    REQUIRE(split.task->split.has_value());
    REQUIRE(split.task->split->method == flux::SplitMethod::Ratio);
    REQUIRE(split.task->split->ratio == 2);
    REQUIRE(split.task->split->entity_type == "box");
    REQUIRE(split.task->split->one_off == false);
}
