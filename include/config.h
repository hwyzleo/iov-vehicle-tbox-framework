#pragma once

#include "config_types.h"
#include <string>
#include <memory>
#include <functional>
#include <vector>

namespace hwyz {
namespace config {

// 前向声明
class ImmutableConfigView;

// 配置管理器类
class ConfigManager {
public:
    // 获取单例实例
    static ConfigManager& instance();

    // 加载配置（非线程安全，应在启动时调用一次）
    // serviceName: 服务名称（如 "prov", "sec", "diag" 等）
    // configRoot: 配置根目录（默认 "/etc/tbox/"）
    // 返回错误码
    ConfigError load(const std::string& serviceName,
                     const std::string& configRoot = "/etc/tbox/");

    // 获取不可变配置快照（线程安全）
    std::shared_ptr<const ImmutableConfigView> getSnapshot() const;

    // 检查是否已加载
    bool isLoaded() const;

    // 获取最后错误信息
    ConfigErrorInfo getLastError() const;

private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

// 不可变配置快照类
class ImmutableConfigView {
public:
    virtual ~ImmutableConfigView() = default;

    // 获取配置值（点分命名空间，如 "log.level"）
    // 返回值通过 std::function 回调，支持类型安全
    virtual bool has(const std::string& key) const = 0;

    // 获取字符串值
    virtual std::string getString(const std::string& key,
                                   const std::string& defaultValue = "") const = 0;

    // 获取整数值
    virtual int getInt(const std::string& key, int defaultValue = 0) const = 0;

    // 获取浮点值
    virtual double getDouble(const std::string& key, double defaultValue = 0.0) const = 0;

    // 获取布尔值
    virtual bool getBool(const std::string& key, bool defaultValue = false) const = 0;

    // 获取字符串列表
    virtual std::vector<std::string> getStringList(const std::string& key) const = 0;

    // 获取嵌套配置（返回新的 ImmutableConfigView）
    virtual std::shared_ptr<const ImmutableConfigView> getSection(const std::string& key) const = 0;

    // 获取所有键
    virtual std::vector<std::string> getKeys() const = 0;
};

// 便捷宏：获取配置管理器实例
#define CONFIG_MANAGER hwyz::config::ConfigManager::instance()

// 便捷宏：获取配置快照
#define CONFIG_SNAPSHOT hwyz::config::ConfigManager::instance().getSnapshot()

} // namespace config
} // namespace hwyz
