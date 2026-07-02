#include "immutable_config_view.h"
#include <sstream>
#include <stdexcept>

namespace hwyz {
namespace config {

ImmutableConfigViewImpl::ImmutableConfigViewImpl(const std::string& yamlStr)
    : m_yamlStr(yamlStr)
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
    }
    return result;
}

std::shared_ptr<const ImmutableConfigView> ImmutableConfigViewImpl::getSection(const std::string& key) const {
    try {
        YAML::Node node = getNode(key);
        if (node && node.IsMap()) {
            return std::make_shared<ImmutableConfigViewImpl>(YAML::Dump(node));
        }
    } catch (...) {
    }
    return nullptr;
}

std::vector<std::string> ImmutableConfigViewImpl::getKeys() const {
    std::vector<std::string> keys;
    try {
        YAML::Node root = YAML::Load(m_yamlStr);
        if (root && root.IsMap()) {
            for (auto it = root.begin(); it != root.end(); ++it) {
                keys.push_back(it->first.as<std::string>());
            }
        }
    } catch (...) {
    }
    return keys;
}

YAML::Node ImmutableConfigViewImpl::getNode(const std::string& key) const {
    std::vector<std::string> parts = splitPath(key);
    YAML::Node current = YAML::Load(m_yamlStr);

    for (const auto& part : parts) {
        if (!current || !current.IsMap()) {
            return YAML::Node();
        }
        current = current[part];
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
