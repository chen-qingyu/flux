#include <catch2/catch_test_macros.hpp>

#include "test_support.hpp"

TEST_CASE("Parser reads any-resource task model", "[parser][any]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "any_resource.bpmn");

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
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "all_resources.bpmn");

    const auto& task = flux::node(model, "Task_service");
    REQUIRE(task.task.has_value());
    REQUIRE(task.task->resource_strategy == flux::ResourceStrategy::All);
    REQUIRE(model.task_resources.at("Task_service").size() == 2);
    REQUIRE(model.resources.at("DataStoreReference_clerk").capacity == 1);
    REQUIRE(model.resources.at("DataStoreReference_pc").capacity == 1);
}

TEST_CASE("Parser reads fifo generator count", "[parser][fifo]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "fifo_queue.bpmn");

    const auto& start = flux::node(model, "Event_start");
    REQUIRE(start.generator.has_value());
    REQUIRE(start.generator->entity_count == 3);
    REQUIRE(start.generator->entity_type == "customer");
}

TEST_CASE("Parser reads external generator times", "[parser][external]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "external_start.bpmn");

    const auto& start = flux::node(model, "Event_external");
    REQUIRE(start.generator.has_value());
    REQUIRE(start.generator->type == flux::InitiatorType::External);
    REQUIRE(start.generator->entity_type == "customer");
    REQUIRE(start.generator->external_records.size() == 4);
    REQUIRE(start.generator->external_records[0].time == 1.0);
    REQUIRE(start.generator->external_records[1].time == 3.0);
    REQUIRE(start.generator->external_records[2].time == 3.0);
    REQUIRE(start.generator->external_records[3].time == 7.5);
}

TEST_CASE("Parser reads weighted splitter model", "[parser][splitter]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "splitter.bpmn");

    const auto& gateway = flux::node(model, "Gateway_splitter");
    REQUIRE(gateway.gateway_criteria == flux::GatewayCriteria::Weight);
    REQUIRE(model.outgoing_flow_ids.at("Gateway_splitter").size() == 3);

    const auto& flow_1 = flux::flow(model, "Flow_07g491b");
    const auto& flow_2 = flux::flow(model, "Flow_1ee3144");
    const auto& flow_3 = flux::flow(model, "Flow_0aoi10x");

    REQUIRE(flow_1.name == "1");
    REQUIRE(flow_1.weight == 1.0);
    REQUIRE(flow_2.weight == 2.0);
    REQUIRE(flow_3.weight == 3.0);
}

TEST_CASE("Parser reads property splitter model", "[parser][splitter-property]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "property_splitter.bpmn");

    const auto& start = flux::node(model, "Event_property");
    REQUIRE(start.generator.has_value());
    REQUIRE(start.generator->type == flux::InitiatorType::External);
    REQUIRE(start.generator->external_records.size() == 4);
    REQUIRE(start.generator->external_records[0].properties.at("action") == "get");
    REQUIRE(start.generator->external_records[1].properties.at("action") == "put");
    REQUIRE(start.generator->external_records[2].properties.at("action") == "put");
    REQUIRE(start.generator->external_records[3].properties.at("action") == "get");

    const auto& gateway = flux::node(model, "Gateway_property");
    REQUIRE(gateway.gateway_criteria == flux::GatewayCriteria::Property);
    REQUIRE(gateway.gateway_property_name == std::optional<std::string>{"action"});

    const auto& get_flow = flux::flow(model, "Flow_property_get");
    const auto& put_flow = flux::flow(model, "Flow_property_put");
    REQUIRE(get_flow.property_value == std::optional<std::string>{"get"});
    REQUIRE(put_flow.property_value == std::optional<std::string>{"put"});
}

TEST_CASE("Parser reads transport task model", "[parser][transport]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "transport.bpmn");

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
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "combine_split.bpmn");

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
    REQUIRE(!combine.task->combine->use_quantity_property);
    REQUIRE(!combine.task->combine->quantity_property.has_value());
    REQUIRE(!combine.task->combine->group_property.has_value());

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

TEST_CASE("Parser reads split property task model", "[parser][split-property]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "split_property.bpmn");

    const auto& start = flux::node(model, "Event_split_property");
    REQUIRE(start.generator.has_value());
    REQUIRE(start.generator->type == flux::InitiatorType::External);
    REQUIRE(start.generator->external_records.size() == 3);
    REQUIRE(start.generator->external_records[0].time == 0.0);
    REQUIRE(start.generator->external_records[0].properties.at("qty") == "1");
    REQUIRE(start.generator->external_records[0].properties.at("route") == "put");
    REQUIRE(start.generator->external_records[1].time == 1.0);
    REQUIRE(start.generator->external_records[1].properties.at("qty") == "2");
    REQUIRE(start.generator->external_records[1].properties.at("route") == "put");
    REQUIRE(start.generator->external_records[2].time == 2.0);
    REQUIRE(start.generator->external_records[2].properties.at("qty") == "3");
    REQUIRE(start.generator->external_records[2].properties.at("route") == "get");

    const auto& split = flux::node(model, "Activity_split_property");
    REQUIRE(split.task.has_value());
    REQUIRE(split.task->type == flux::TaskType::Split);
    REQUIRE(split.task->split.has_value());
    REQUIRE(split.task->split->method == flux::SplitMethod::Property);
    REQUIRE(split.task->split->property_name == std::optional<std::string>{"qty"});
    REQUIRE(split.task->split->entity_type.empty());
    REQUIRE(split.task->split->one_off == true);

    const auto& gateway = flux::node(model, "Gateway_route");
    REQUIRE(gateway.gateway_criteria == flux::GatewayCriteria::Property);
    REQUIRE(gateway.gateway_property_name == std::optional<std::string>{"route"});
}

TEST_CASE("Parser reads quantity-aware combine restore model", "[parser][combine][quantity]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "combine_quantity_restore.bpmn");

    const auto& combine = flux::node(model, "Activity_combine");
    REQUIRE(combine.task.has_value());
    REQUIRE(combine.task->type == flux::TaskType::Combine);
    REQUIRE(combine.task->combine.has_value());
    REQUIRE(combine.task->combine->method == flux::CombineMethod::Ratio);
    REQUIRE(combine.task->combine->ratio == 5.0);
    REQUIRE(combine.task->combine->entity_type == "bundle");
    REQUIRE(combine.task->combine->use_quantity_property);
    REQUIRE(combine.task->combine->quantity_property == std::optional<std::string>{"qty"});
    REQUIRE(!combine.task->combine->group_property.has_value());
}

TEST_CASE("Parser reads group-ratio combine model", "[parser][combine][group-ratio]")
{
    const auto model = flux::Parser::parse(std::filesystem::path("data") / "tests" / "group_ratio_restore.bpmn");

    const auto& combine = flux::node(model, "Activity_combine");
    REQUIRE(combine.task.has_value());
    REQUIRE(combine.task->type == flux::TaskType::Combine);
    REQUIRE(combine.task->combine.has_value());
    REQUIRE(combine.task->combine->method == flux::CombineMethod::GroupRatio);
    REQUIRE(combine.task->combine->ratio == 2.0);
    REQUIRE(combine.task->combine->entity_type == "batch");
    REQUIRE(combine.task->combine->use_quantity_property);
    REQUIRE(combine.task->combine->quantity_property == std::optional<std::string>{"qty"});
    REQUIRE(combine.task->combine->group_property == std::optional<std::string>{"color"});
}
