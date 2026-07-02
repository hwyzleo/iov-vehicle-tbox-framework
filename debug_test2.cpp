#include "config_validator.h"
#include <iostream>

using namespace hwyz::config;

int main() {
    YAML::Node config;
    config["server"]["port"][0] = 8080;
    config["server"]["port"][1] = 9090;

    std::string path = "server.port";
    
    // Manual path split
    std::vector<std::string> parts;
    std::istringstream stream(path);
    std::string part;
    while (std::getline(stream, part, '.')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    std::cout << "Path parts: ";
    for (const auto& p : parts) {
        std::cout << p << " ";
    }
    std::cout << std::endl;

    // Manual node traversal
    YAML::Node current = config;
    for (const auto& part : parts) {
        std::cout << "Checking part: " << part << std::endl;
        std::cout << "current valid: " << (current ? "true" : "false") << std::endl;
        std::cout << "current IsMap: " << current.IsMap() << std::endl;
        std::cout << "current[part] valid: " << (current[part] ? "true" : "false") << std::endl;
        
        if (!current || !current.IsMap() || !current[part]) {
            std::cout << "Node does not exist!" << std::endl;
            return 1;
        }
        current = current[part];
    }

    std::cout << "Final node exists: true" << std::endl;
    std::cout << "Final node IsScalar: " << current.IsScalar() << std::endl;
    std::cout << "Final node IsSequence: " << current.IsSequence() << std::endl;

    return 0;
}
