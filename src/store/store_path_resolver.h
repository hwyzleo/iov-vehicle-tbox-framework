#pragma once

#include <string>

namespace hwyz {
namespace store {

class PathResolver {
public:
    PathResolver(const std::string& serviceName, const std::string& storeRoot = "/var/lib/tbox");
    ~PathResolver() = default;

    std::string getStorePath() const;
    std::string getKeyPath(const std::string& key) const;
    std::string getTempPath(const std::string& key) const;

    bool ensureDirectory() const;
    bool directoryExists() const;

    std::string getServiceName() const;

private:
    std::string m_serviceName;
    std::string m_storeRoot;

    std::string normalizePath(const std::string& path) const;
};

} // namespace store
} // namespace hwyz
