#include "path_resolver.h"
#include <sys/stat.h>
#include <unistd.h>
#include <climits>

namespace hwyz {
namespace config {

// --- 单根目录构造（向后兼容） ---
PathResolver::PathResolver(const std::string& serviceName, const std::string& configRoot)
    : m_serviceName(serviceName)
    , m_configRoot(configRoot)
    , m_configRoots({configRoot})
{
    if (!m_configRoot.empty() && m_configRoot.back() != '/') {
        m_configRoot += '/';
        m_configRoots[0] = m_configRoot;
    }
}

// --- 多根目录构造 ---
PathResolver::PathResolver(const std::string& serviceName, const std::vector<std::string>& configRoots)
    : m_serviceName(serviceName)
    , m_configRoots(configRoots)
{
    // 确保每个根目录以 '/' 结尾
    for (auto& root : m_configRoots) {
        if (!root.empty() && root.back() != '/') {
            root += '/';
        }
    }
    // 向后兼容：m_configRoot 取第一个
    m_configRoot = m_configRoots.empty() ? "" : m_configRoots[0];
}

// --- 静态工具 ---
bool PathResolver::fileExists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

// --- 多根目录查找 ---
std::pair<std::string, int> PathResolver::findCommon() const {
    for (int i = 0; i < static_cast<int>(m_configRoots.size()); ++i) {
        std::string path = m_configRoots[i] + "common.yaml";
        if (fileExists(path)) {
            return {path, i};
        }
    }
    return {"", -1};
}

std::pair<std::string, int> PathResolver::findService() const {
    for (int i = 0; i < static_cast<int>(m_configRoots.size()); ++i) {
        std::string path = m_configRoots[i] + "conf.d/" + m_serviceName + ".yaml";
        if (fileExists(path)) {
            return {path, i};
        }
    }
    return {"", -1};
}

// --- 多根目录：resolveAll ---
std::vector<ConfigEntry> PathResolver::resolveAll() const {
    std::vector<ConfigEntry> entries;

    // 1. common.yaml（root 层）：在所有根目录中查找，只取第一个存在的
    std::pair<std::string, int> commonResult = findCommon();
    if (!commonResult.first.empty()) {
        entries.push_back({commonResult.first, "common"});
    }

    // 2. ./config/common.yaml（项目层 common，覆盖 root common）
    std::string projectCommonPath = "./config/common.yaml";
    if (fileExists(projectCommonPath)) {
        entries.push_back({projectCommonPath, "project/common"});
    }

    // 3. conf.d/<svc>.yaml：在所有根目录中查找，只取第一个存在的
    std::pair<std::string, int> serviceResult = findService();
    if (!serviceResult.first.empty()) {
        entries.push_back({serviceResult.first, "conf.d/" + m_serviceName});
    }

    // 4. ./config/<svc>.yaml（项目级覆盖，相对于当前工作目录）
    //    这是旧 PathResolver.getProjectPath() 的行为，保持向后兼容
    std::string projectPath = "./config/" + m_serviceName + ".yaml";
    if (fileExists(projectPath)) {
        entries.push_back({projectPath, "project/" + m_serviceName});
    }

    // 5. 每个根目录下的 <svc>.yaml（目录级覆盖）
    for (int i = 0; i < static_cast<int>(m_configRoots.size()); ++i) {
        std::string path = m_configRoots[i] + m_serviceName + ".yaml";
        if (fileExists(path)) {
            entries.push_back({path, "root[" + std::to_string(i) + "]/" + m_serviceName});
        }
    }

    // 6. ./<svc>.yaml（目录级本地覆盖，最高优先）
    std::string localPath = "./" + m_serviceName + ".yaml";
    if (fileExists(localPath)) {
        entries.push_back({localPath, "local/" + m_serviceName});
    }

    return entries;
}

// --- 以下为单根目录 API（向后兼容，保持原有行为） ---

std::string PathResolver::getCommonPath() const {
    return m_configRoot + "common.yaml";
}

std::string PathResolver::getServicePath() const {
    return m_configRoot + "conf.d/" + m_serviceName + ".yaml";
}

std::string PathResolver::getProjectPath() const {
    return "./config/" + m_serviceName + ".yaml";
}

std::string PathResolver::getLocalPath() const {
    return "./" + m_serviceName + ".yaml";
}

bool PathResolver::commonExists() const {
    return fileExists(getCommonPath());
}

bool PathResolver::serviceExists() const {
    return fileExists(getServicePath());
}

bool PathResolver::projectExists() const {
    return fileExists(getProjectPath());
}

bool PathResolver::localExists() const {
    return fileExists(getLocalPath());
}

std::string PathResolver::getServiceName() const {
    return m_serviceName;
}

std::string PathResolver::getConfigRoot() const {
    return m_configRoot;
}

} // namespace config
} // namespace hwyz
