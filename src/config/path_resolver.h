#pragma once

#include "config_types.h"
#include <string>
#include <vector>

namespace hwyz {
namespace config {

class PathResolver {
public:
    PathResolver(const std::string& serviceName, const std::string& configRoot);
    ~PathResolver() = default;

    std::string getCommonPath() const;
    std::string getServicePath() const;
    std::string getLocalPath() const;

    bool commonExists() const;
    bool serviceExists() const;
    bool localExists() const;

    std::string getServiceName() const;
    std::string getConfigRoot() const;

private:
    std::string m_serviceName;
    std::string m_configRoot;
};

} // namespace config
} // namespace hwyz
