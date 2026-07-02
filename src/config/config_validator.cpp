#include "config_validator.h"
#include <sstream>
#include <iostream>

namespace hwyz {
namespace config {

void ConfigValidator::addRule(const ValidationRule& rule) {
    m_rules.push_back(rule);
}

void ConfigValidator::addRules(const std::vector<ValidationRule>& rules) {
    m_rules.insert(m_rules.end(), rules.begin(), rules.end());
}

ConfigErrorInfo ConfigValidator::validate(const YAML::Node& config) const {
    for (const auto& rule : m_rules) {
        YAML::Node node = getNode(config, rule.path);

        if (!node || node.IsNull()) {
            if (rule.required) {
                return {ConfigError::kValidationFailed,
                        "Required field missing: " + rule.description,
                        rule.path};
            }
            continue;
        }

        if (!checkType(node, rule.expectedType)) {
            return {ConfigError::kValidationFailed,
                    "Type mismatch for field: " + rule.description,
                    rule.path};
        }
    }

    return {ConfigError::kOk, "", ""};
}

void ConfigValidator::clearRules() {
    m_rules.clear();
}

bool ConfigValidator::nodeExists(const YAML::Node& config, const std::string& path) const {
    std::vector<std::string> parts = splitPath(path);
    YAML::Node current = config;

    for (const auto& part : parts) {
        if (!current || !current.IsMap()) {
            return false;
        }
        
        // Check if key exists without creating null node
        if (!current[part]) {
            return false;
        }
        
        current = current[part];
    }

    return true;
}

YAML::Node ConfigValidator::getNode(const YAML::Node& config, const std::string& path) const {
    std::vector<std::string> parts = splitPath(path);
    YAML::Node current = config;

    for (const auto& part : parts) {
        if (!current || !current.IsMap()) {
            return YAML::Node();
        }
        current = current[part];
    }

    return current;
}

bool ConfigValidator::checkType(const YAML::Node& node, ConfigType expectedType) const {
    if (!node) {
        return false;
    }

    switch (expectedType) {
        case ConfigType::kScalar:
            return node.IsScalar();
        case ConfigType::kSequence:
            return node.IsSequence();
        case ConfigType::kMap:
            return node.IsMap();
        default:
            return false;
    }
}

std::vector<std::string> ConfigValidator::splitPath(const std::string& path) const {
    std::vector<std::string> parts;
    std::istringstream stream(path);
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
