#include "immutable_config_view.h"
#include <sstream>

namespace hwyz {
namespace config {

ImmutableConfigViewImpl::ImmutableConfigViewImpl(const YAML::Node& root)
    : m_yamlStr(YAML::Dump(root))  // 序列化为字符串确保不可变
{
}

bool ImmutableConfigViewImpl::has(const std::string& key) const {
    try {
        YAML::Node node = getNode(key);
        return node && !node.IsNull();
    } catch (...) {
        return false;
    }
}

std::string ImmutableConfigViewImpl::getString(const std::string& key, const std::string& defaultValue) const {
    try {
        YAML::Node node = getNode(key);
        if (node && node.IsScalar()) {
            return node.as<std::string>();
        }
    } catch (...) {
        // 忽略异常
    }
    return defaultValue;
}

int ImmutableConfigViewImpl::getInt(const std::string& key, int defaultValue) const {
    try {
        YAML::Node node = getNode(key);
        if (node && node.IsScalar()) {
            return node.as<int>();
        }
    } catch (...) {
        // 忽略异常
    }
    return defaultValue;
}

double ImmutableConfigViewImpl::getDouble(const std::string& key, double defaultValue) const {
    try {
        YAML::Node node = getNode(key);
        if (node && node.IsScalar()) {
            return node.as<double>();
        }
    } catch (...) {
        // 忽略异常
    }
    return defaultValue;
}

bool ImmutableConfigViewImpl::getBool(const std::string& key, bool defaultValue) const {
    try {
        YAML::Node node = getNode(key);
        if (node && node.IsScalar()) {
            return node.as<bool>();
        }
    } catch (...) {
        // 忽略异常
    }
    return defaultValue;
}

std::vector<std::string> ImmutableConfigViewImpl::getStringList(const std::string& key) const {
    std::vector<std::string> result;
    try {
        YAML::Node node = getNode(key);
        if (node && node.IsSequence()) {
            for (const auto& item : node) {
                if (item.IsScalar()) {
                    result.push_back(item.as<std::string>());
                }
            }
        }
    } catch (...) {
        // 忽略异常
    }
    return result;
}

std::shared_ptr<const ImmutableConfigView> ImmutableConfigViewImpl::getSection(const std::string& key) const {
    try {
        YAML::Node node = getNode(key);
        if (node && node.IsMap()) {
            return std::make_shared<ImmutableConfigViewImpl>(node);
        }
    } catch (...) {
        // 忽略异常
    }
    return nullptr;
}

std::vector<std::string> ImmutableConfigViewImpl::getKeys() const {
    std::vector<std::string> keys;
    YAML::Node root = YAML::Load(m_yamlStr);
    if (root && root.IsMap()) {
        for (auto it = root.begin(); it != root.end(); ++it) {
            keys.push_back(it->first.as<std::string>());
        }
    }
    return keys;
}

YAML::Node ImmutableConfigViewImpl::getNode(const std::string& key) const {
    YAML::Node root = YAML::Load(m_yamlStr);
    std::vector<std::string> parts = splitPath(key);
    YAML::Node current = root;

    for (const auto& part : parts) {
        if (!current) {
            return YAML::Node();
        }

        // For map nodes, access the key
        if (current.IsMap()) {
            current = current[part];
        } else {
            // For non-map nodes, return null
            return YAML::Node();
        }
    }

    return current;
}

std::vector<std::string> ImmutableConfigViewImpl::splitPath(const std::string& key) const {
    std::vector<std::string> parts;
    std::istringstream stream(key);
    std::string part;

    while (std::getline(stream, part, '.')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    return parts;
}

} // namespace config
} // namespace hwyz
