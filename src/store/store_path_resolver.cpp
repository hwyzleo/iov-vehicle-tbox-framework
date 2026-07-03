#include "store_path_resolver.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdexcept>

namespace hwyz {
namespace store {

PathResolver::PathResolver(const std::string& serviceName, const std::string& storeRoot)
    : m_serviceName(serviceName)
    , m_storeRoot(storeRoot)
{
    if (m_serviceName.empty()) {
        throw std::invalid_argument("Service name cannot be empty");
    }

    if (!m_storeRoot.empty() && m_storeRoot.back() != '/') {
        m_storeRoot += '/';
    }
}

std::string PathResolver::getStorePath() const {
    return m_storeRoot + m_serviceName + "/";
}

std::string PathResolver::getKeyPath(const std::string& key) const {
    return getStorePath() + key + ".dat";
}

std::string PathResolver::getTempPath(const std::string& key) const {
    return getKeyPath(key) + ".tmp";
}

bool PathResolver::ensureDirectory() const {
    struct stat st;
    if (stat(m_storeRoot.c_str(), &st) != 0) {
        if (mkdir(m_storeRoot.c_str(), 0700) != 0) {
            return false;
        }
    }

    std::string servicePath = getStorePath();
    if (stat(servicePath.c_str(), &st) != 0) {
        if (mkdir(servicePath.c_str(), 0700) != 0) {
            return false;
        }
    }

    return true;
}

bool PathResolver::directoryExists() const {
    struct stat st;
    return (stat(getStorePath().c_str(), &st) == 0 && (st.st_mode & S_IFDIR));
}

std::string PathResolver::getServiceName() const {
    return m_serviceName;
}

std::string PathResolver::normalizePath(const std::string& path) const {
    std::string result = path;
    while (!result.empty() && result.back() == '/') {
        result.pop_back();
    }
    return result;
}

} // namespace store
} // namespace hwyz
