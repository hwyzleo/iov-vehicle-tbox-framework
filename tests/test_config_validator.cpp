#include "config_validator.h"
#include <cassert>
#include <iostream>

using namespace hwyz::config;

void test_validate_required_field() {
    ConfigValidator validator;
    validator.addRule({"log.level", ConfigType::kScalar, true, "Log level"});

    YAML::Node config;
    config["log"]["level"] = "info";

    ConfigErrorInfo error = validator.validate(config);
    assert(error.code == ConfigError::kOk);

    std::cout << "test_validate_required_field passed" << std::endl;
}

void test_validate_missing_required() {
    ConfigValidator validator;
    validator.addRule({"log.level", ConfigType::kScalar, true, "Log level"});

    YAML::Node config;
    config["log"]["format"] = "json";

    ConfigErrorInfo error = validator.validate(config);
    assert(error.code == ConfigError::kValidationFailed);
    assert(error.path == "log.level");

    std::cout << "test_validate_missing_required passed" << std::endl;
}

void test_validate_type_mismatch() {
    ConfigValidator validator;
    validator.addRule({"server.port", ConfigType::kScalar, true, "Server port"});

    YAML::Node config;
    config["server"]["port"][0] = 8080;
    config["server"]["port"][1] = 9090;

    ConfigErrorInfo error = validator.validate(config);
    assert(error.code == ConfigError::kValidationFailed);

    std::cout << "test_validate_type_mismatch passed" << std::endl;
}

void test_validate_optional_field() {
    ConfigValidator validator;
    validator.addRule({"log.file", ConfigType::kScalar, false, "Log file path"});

    YAML::Node config;
    config["log"]["level"] = "info";

    ConfigErrorInfo error = validator.validate(config);
    assert(error.code == ConfigError::kOk);

    std::cout << "test_validate_optional_field passed" << std::endl;
}

int main() {
    test_validate_required_field();
    test_validate_missing_required();
    test_validate_type_mismatch();
    test_validate_optional_field();
    return 0;
}
