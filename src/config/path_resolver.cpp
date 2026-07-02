#include "path_resolver.h"
#include <sys/stat.h>

namespace hwyz {
namespace config {

PathResolver::PathResolver(const std::string& serviceName, const std::string& configRoot)
    : m_serviceName(serviceName)
    , m_configRoot(configRoot)
{
    if (!m_configRoot.empty() && m_configRoot.back() != '/') {
        m_configRoot += '/';
    }
}

std::string PathResolver::getCommonPath() const {
    return m_configRoot + "common.yaml";
}

std::string PathResolver::getServicePath() const {
    return m_configRoot + "conf.d/" + m_serviceName + ".yaml";
}

std::string PathResolver::getLocalPath() const {
    return "./" + m_serviceName + ".yaml";
}

bool PathResolver::commonExists() const {
    struct stat buffer;
    return (stat(getCommonPath().c_str(), &buffer) == 0);
}

bool PathResolver::serviceExists() const {
    struct stat buffer;
    return (stat(getServicePath().c_str(), &buffer) == 0);
}

bool PathResolver::localExists() const {
    struct stat buffer;
    return (stat(getLocalPath().c_str(), &buffer) == 0);
}

std::string PathResolver::getServiceName() const {
    return m_serviceName;
}

std::string PathResolver::getConfigRoot() const {
    return m_configRoot;
}

} // namespace config
} // namespace hwyz
