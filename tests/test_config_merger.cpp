#include "config_merger.h"
#include <cassert>
#include <iostream>

using namespace hwyz::config;

void test_merge_scalar_override() {
    ConfigMerger merger;

    YAML::Node base;
    base["key"] = "value1";

    YAML::Node override;
    override["key"] = "value2";

    YAML::Node result = merger.merge(base, override);
    assert(result["key"].as<std::string>() == "value2");

    std::cout << "test_merge_scalar_override passed" << std::endl;
}

void test_merge_map_deep() {
    ConfigMerger merger;

    YAML::Node base;
    base["server"]["host"] = "localhost";
    base["server"]["port"] = 8080;

    YAML::Node override;
    override["server"]["port"] = 9090;
    override["server"]["timeout"] = 30;

    YAML::Node result = merger.merge(base, override);
    assert(result["server"]["host"].as<std::string>() == "localhost");
    assert(result["server"]["port"].as<int>() == 9090);
    assert(result["server"]["timeout"].as<int>() == 30);

    std::cout << "test_merge_map_deep passed" << std::endl;
}

void test_merge_array_override() {
    ConfigMerger merger;

    YAML::Node base;
    base["items"][0] = "item1";
    base["items"][1] = "item2";

    YAML::Node override;
    override["items"][0] = "item3";

    YAML::Node result = merger.merge(base, override);
    assert(result["items"].size() == 1);
    assert(result["items"][0].as<std::string>() == "item3");

    std::cout << "test_merge_array_override passed" << std::endl;
}

void test_merge_multiple() {
    ConfigMerger merger;

    YAML::Node node1;
    node1["key1"] = "value1";

    YAML::Node node2;
    node2["key2"] = "value2";

    YAML::Node node3;
    node3["key1"] = "override1";

    std::vector<YAML::Node> nodes = {node1, node2, node3};
    YAML::Node result = merger.mergeMultiple(nodes);

    assert(result["key1"].as<std::string>() == "override1");
    assert(result["key2"].as<std::string>() == "value2");

    std::cout << "test_merge_multiple passed" << std::endl;
}

int main() {
    test_merge_scalar_override();
    test_merge_map_deep();
    test_merge_array_override();
    test_merge_multiple();
    return 0;
}
