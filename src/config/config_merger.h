#pragma once

#include "config_types.h"
#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>

namespace hwyz {
namespace config {

class ConfigMerger {
public:
    ConfigMerger() = default;
    ~ConfigMerger() = default;

    YAML::Node merge(const YAML::Node& base, const YAML::Node& override);
    YAML::Node mergeMultiple(const std::vector<YAML::Node>& nodes);

private:
    void mergeMap(YAML::Node& base, const YAML::Node& override);
    bool isMap(const YAML::Node& node) const;
};

} // namespace config
} // namespace hwyz
