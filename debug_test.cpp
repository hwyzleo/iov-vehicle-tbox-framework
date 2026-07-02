#include "config_validator.h"
#include <iostream>

using namespace hwyz::config;

int main() {
    ConfigValidator validator;
    validator.addRule({"server.port", ConfigType::kScalar, true, "Server port"});

    YAML::Node config;
    config["server"]["port"][0] = 8080;
    config["server"]["port"][1] = 9090;

    std::cout << "Config: " << config << std::endl;
    std::cout << "server.port exists: " << (config["server"]["port"] ? "true" : "false") << std::endl;
    std::cout << "server.port IsScalar: " << config["server"]["port"].IsScalar() << std::endl;
    std::cout << "server.port IsSequence: " << config["server"]["port"].IsSequence() << std::endl;

    ConfigErrorInfo error = validator.validate(config);
    std::cout << "Error code: " << static_cast<int>(error.code) << std::endl;
    std::cout << "Error message: " << error.message << std::endl;
    std::cout << "Error path: " << error.path << std::endl;

    return 0;
}
