#include "config_merger.h"
#include <sstream>

namespace hwyz {
namespace config {

static std::string dumpNodeToYaml(const YAML::Node& node) {
    std::ostringstream out;
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            std::string key = it->first.as<std::string>();
            YAML::Node value = it->second;
            if (value.IsMap()) {
                out << key << ":\n";
                for (auto it2 = value.begin(); it2 != value.end(); ++it2) {
                    std::string k2 = it2->first.as<std::string>();
                    YAML::Node v2 = it2->second;
                    if (v2.IsScalar()) {
                        out << "  " << k2 << ": " << v2.as<std::string>() << "\n";
                    } else if (v2.IsSequence()) {
                        out << "  " << k2 << ":\n";
                        for (auto item : v2) {
                            out << "    - " << item.as<std::string>() << "\n";
                        }
                    }
                }
            } else if (value.IsScalar()) {
                out << key << ": " << value.as<std::string>() << "\n";
            } else if (value.IsSequence()) {
                out << key << ":\n";
                for (auto item : value) {
                    out << "  - " << item.as<std::string>() << "\n";
                }
            }
        }
    }
    return out.str();
}

static void mergeMapInto(YAML::Node& base, const YAML::Node& overlay) {
    for (auto it = overlay.begin(); it != overlay.end(); ++it) {
        std::string key = it->first.as<std::string>();
        YAML::Node overlayVal = it->second;
        YAML::Node baseVal = base[key];

        if (baseVal && baseVal.IsMap() && overlayVal.IsMap()) {
            mergeMapInto(baseVal, overlayVal);
        } else {
            base[key] = overlayVal;
        }
    }
}

YAML::Node ConfigMerger::merge(const YAML::Node& base, const YAML::Node& override) {
    if (!base || base.IsNull()) {
        return YAML::Clone(override);
    }

    if (!override || override.IsNull()) {
        return YAML::Clone(base);
    }

    if (isMap(base) && isMap(override)) {
        YAML::Node result = YAML::Clone(base);
        mergeMapInto(result, override);
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

std::string ConfigMerger::mergeMultipleToString(const std::vector<YAML::Node>& nodes) {
    YAML::Node merged = mergeMultiple(nodes);
    return dumpNodeToYaml(merged);
}

void ConfigMerger::mergeMap(YAML::Node& base, const YAML::Node& override) {
    mergeMapInto(base, override);
}

bool ConfigMerger::isMap(const YAML::Node& node) const {
    return node && node.IsMap();
}

} // namespace config
} // namespace hwyz
