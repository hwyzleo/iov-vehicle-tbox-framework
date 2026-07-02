#include <yaml-cpp/yaml.h>
#include <iostream>

int main() {
    YAML::Node config;
    config["log"]["level"] = "info";

    std::cout << "Direct access:" << std::endl;
    std::cout << "config[\"log\"][\"level\"] valid: " << (config["log"]["level"] ? "true" : "false") << std::endl;
    std::cout << "config[\"log\"][\"level\"] IsScalar: " << config["log"]["level"].IsScalar() << std::endl;
    std::cout << "config[\"log\"][\"level\"] IsSequence: " << config["log"]["level"].IsSequence() << std::endl;
    std::cout << "config[\"log\"][\"level\"] IsMap: " << config["log"]["level"].IsMap() << std::endl;

    std::cout << "\nIndirect access through variables:" << std::endl;
    YAML::Node logNode = config["log"];
    std::cout << "logNode valid: " << (logNode ? "true" : "false") << std::endl;
    std::cout << "logNode IsMap: " << logNode.IsMap() << std::endl;
    
    YAML::Node levelNode = logNode["level"];
    std::cout << "levelNode valid: " << (levelNode ? "true" : "false") << std::endl;
    std::cout << "levelNode IsScalar: " << levelNode.IsScalar() << std::endl;
    std::cout << "levelNode IsSequence: " << levelNode.IsSequence() << std::endl;
    std::cout << "levelNode IsMap: " << levelNode.IsMap() << std::endl;

    return 0;
}
