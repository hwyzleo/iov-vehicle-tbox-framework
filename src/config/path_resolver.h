#pragma once

#include "config_types.h"
#include <string>
#include <vector>

namespace hwyz {
namespace config {

// 已发现的配置文件条目
struct ConfigEntry {
    std::string path;   // 文件绝对/相对路径
    std::string layer;  // 层级描述（用于日志）
};

class PathResolver {
public:
    // 单根目录构造（向后兼容）
    PathResolver(const std::string& serviceName, const std::string& configRoot);

    // 多根目录构造（优先级：roots[0] 最低，roots.back() 最高）
    PathResolver(const std::string& serviceName, const std::vector<std::string>& configRoots);

    ~PathResolver() = default;

    // 以下为单根目录 API（向后兼容）
    std::string getCommonPath() const;
    std::string getServicePath() const;
    std::string getProjectPath() const;
    std::string getLocalPath() const;

    bool commonExists() const;
    bool serviceExists() const;
    bool projectExists() const;
    bool localExists() const;

    std::string getServiceName() const;
    std::string getConfigRoot() const;

    // 多根目录 API：按优先级从低到高返回所有已存在的配置文件
    // 优先级：common < conf.d/svc < roots[0]/svc < roots[1]/svc < ...
    std::vector<ConfigEntry> resolveAll() const;

private:
    std::string m_serviceName;
    std::string m_configRoot;                // 单根目录（向后兼容）
    std::vector<std::string> m_configRoots;  // 多根目录

    // 检查文件是否存在
    static bool fileExists(const std::string& path);

    // 在多个根目录中查找第一个存在的 common.yaml
    // 返回 {path, rootIndex}，未找到返回 {"", -1}
    std::pair<std::string, int> findCommon() const;

    // 在多个根目录中查找第一个存在的 conf.d/<svc>.yaml
    std::pair<std::string, int> findService() const;
};

} // namespace config
} // namespace hwyz
