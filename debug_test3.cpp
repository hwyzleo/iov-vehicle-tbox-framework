#include "config_validator.h"
#include <iostream>
#include <sstream>

using namespace hwyz::config;

// Inline debug version of validate
ConfigErrorInfo validateDebug(const ConfigValidator& validator, const YAML::Node& config) {
    ValidationRule rule = {"server.port", ConfigType::kScalar, true, "Server port"};
    
    std::cout << "Rule: path=" << rule.path << ", required=" << rule.required << std::endl;
    
    // Check if node exists
    std::vector<std::string> parts;
    std::istringstream stream(rule.path);
    std::string part;
    while (std::getline(stream, part, '.')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    YAML::Node current = config;
    bool exists = true;
    for (const auto& p : parts) {
        if (!current || !current.IsMap() || !current[p]) {
            exists = false;
            break;
        }
        current = current[p];
    }

    std::cout << "nodeExists: " << exists << std::endl;
    
    if (rule.required && !exists) {
        std::cout << "Returning: Required field missing" << std::endl;
        return {ConfigError::kValidationFailed, "Required field missing: " + rule.description, rule.path};
    }

    if (exists) {
        YAML::Node node = current;
        std::cout << "Node valid: " << (node ? "true" : "false") << std::endl;
        std::cout << "Node IsScalar: " << node.IsScalar() << std::endl;
        
        bool typeOk = node.IsScalar();
        std::cout << "checkType result: " << typeOk << std::endl;
        
        if (!typeOk) {
            std::cout << "Returning: Type mismatch" << std::endl;
            return {ConfigError::kValidationFailed, "Type mismatch for field: " + rule.description, rule.path};
        }
    }

    std::cout << "Returning: kOk" << std::endl;
    return {ConfigError::kOk, "", ""};
}

int main() {
    ConfigValidator validator;
    YAML::Node config;
    config["server"]["port"][0] = 8080;
    config["server"]["port"][1] = 9090;

    ConfigErrorInfo error = validateDebug(validator, config);
    std::cout << "Error code: " << static_cast<int>(error.code) << std::endl;

    return 0;
}
