#include "config_merger.h"

namespace hwyz {
namespace config {

YAML::Node ConfigMerger::merge(const YAML::Node& base, const YAML::Node& override) {
    if (!base || base.IsNull()) {
        return YAML::Clone(override);
    }

    if (!override || override.IsNull()) {
        return YAML::Clone(base);
    }

    if (isMap(base) && isMap(override)) {
        YAML::Node result = YAML::Clone(base);
        mergeMap(result, override);
        return result;
    }

    return YAML::Clone(override);
}

YAML::Node ConfigMerger::mergeMultiple(const std::vector<YAML::Node>& nodes) {
    if (nodes.empty()) {
        return YAML::Node(YAML::NodeType::Null);
    }

    YAML::Node result = YAML::Clone(nodes[0]);
    for (size_t i = 1; i < nodes.size(); ++i) {
        result = merge(result, nodes[i]);
    }
    return result;
}

void ConfigMerger::mergeMap(YAML::Node& base, const YAML::Node& override) {
    for (auto it = override.begin(); it != override.end(); ++it) {
        std::string key = it->first.as<std::string>();
        YAML::Node overrideValue = it->second;

        if (base[key]) {
            YAML::Node baseValue = base[key];

            if (isMap(baseValue) && isMap(overrideValue)) {
                mergeMap(baseValue, overrideValue);
            } else {
                base[key] = YAML::Clone(overrideValue);
            }
        } else {
            base[key] = YAML::Clone(overrideValue);
        }
    }
}

bool ConfigMerger::isMap(const YAML::Node& node) const {
    return node && node.IsMap();
}

} // namespace config
} // namespace hwyz
