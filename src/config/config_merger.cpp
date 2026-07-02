#include "config_merger.h"

namespace hwyz {
namespace config {

YAML::Node ConfigMerger::merge(const YAML::Node& base, const YAML::Node& override) {
    // 如果 base 为空，直接返回 override
    if (!base || base.IsNull()) {
        return YAML::Clone(override);
    }

    // 如果 override 为空，返回 base
    if (!override || override.IsNull()) {
        return YAML::Clone(base);
    }

    // 如果两者都是 map，递归合并
    if (isMap(base) && isMap(override)) {
        YAML::Node result = YAML::Clone(base);
        mergeMap(result, override);
        return result;
    }

    // 否则，override 整体覆盖 base
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

        // 如果 base 中存在该 key
        if (base[key]) {
            YAML::Node baseValue = base[key];

            // 如果两者都是 map，递归合并
            if (isMap(baseValue) && isMap(overrideValue)) {
                mergeMap(baseValue, overrideValue);
            } else {
                // 否则，override 整体覆盖
                base[key] = YAML::Clone(overrideValue);
            }
        } else {
            // base 中不存在该 key，直接添加
            base[key] = YAML::Clone(overrideValue);
        }
    }
}

bool ConfigMerger::isMap(const YAML::Node& node) const {
    return node && node.IsMap();
}

} // namespace config
} // namespace hwyz
