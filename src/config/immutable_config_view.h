#pragma once

#include "config.h"
#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace hwyz {
namespace config {

// 不可变配置快照实现
class ImmutableConfigViewImpl : public ImmutableConfigView {
public:
    // 从 YAML 节点构造（深拷贝，确保不可变）
    explicit ImmutableConfigViewImpl(const YAML::Node& root);
    ~ImmutableConfigViewImpl() = default;

    // ImmutableConfigView 接口实现
    bool has(const std::string& key) const override;
    std::string getString(const std::string& key, const std::string& defaultValue = "") const override;
    int getInt(const std::string& key, int defaultValue = 0) const override;
    double getDouble(const std::string& key, double defaultValue = 0.0) const override;
    bool getBool(const std::string& key, bool defaultValue = false) const override;
    std::vector<std::string> getStringList(const std::string& key) const override;
    std::shared_ptr<const ImmutableConfigView> getSection(const std::string& key) const override;
    std::vector<std::string> getKeys() const override;

private:
    // 获取节点
    YAML::Node getNode(const std::string& key) const;

    // 分割点分路径
    std::vector<std::string> splitPath(const std::string& key) const;

    // 不可变的 YAML 根节点
    const YAML::Node m_root;

    // 缓存互斥锁（用于线程安全的缓存访问）
    mutable std::mutex m_cacheMutex;
};

} // namespace config
} // namespace hwyz