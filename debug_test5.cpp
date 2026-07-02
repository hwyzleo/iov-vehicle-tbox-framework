#include <yaml-cpp/yaml.h>
#include <iostream>

int main() {
    YAML::Node config;
    config["log"]["level"] = "info";

    std::cout << "Testing node types:" << std::endl;
    
    // Get the node through path traversal like the validator does
    YAML::Node current = config;
    
    // First part: "log"
    std::cout << "Before 'log':" << std::endl;
    std::cout << "  current valid: " << (current ? "true" : "false") << std::endl;
    std::cout << "  current IsMap: " << current.IsMap() << std::endl;
    std::cout << "  current[\"log\"] valid: " << (current["log"] ? "true" : "false") << std::endl;
    
    current = current["log"];
    std::cout << "After 'log':" << std::endl;
    std::cout << "  current valid: " << (current ? "true" : "false") << std::endl;
    std::cout << "  current IsMap: " << current.IsMap() << std::endl;
    std::cout << "  current[\"level\"] valid: " << (current["level"] ? "true" : "false") << std::endl;
    
    current = current["level"];
    std::cout << "After 'level':" << std::endl;
    std::cout << "  current valid: " << (current ? "true" : "false") << std::endl;
    std::cout << "  current type: " << current.Type() << std::endl;
    std::cout << "  current IsScalar: " << current.IsScalar() << std::endl;
    std::cout << "  current IsNull: " << current.IsNull() << std::endl;
    
    // Try to get the value
    try {
        std::string value = current.as<std::string>();
        std::cout << "  current value: " << value << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  Exception: " << e.what() << std::endl;
    }

    return 0;
}
