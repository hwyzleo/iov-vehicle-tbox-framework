#pragma once

#include "config.h"
#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>
#include <memory>

namespace hwyz {
namespace config {

// 不可变配置快照实现
class ImmutableConfigViewImpl : public ImmutableConfigView {
public:
    // 从 YAML 字符串构造
    explicit ImmutableConfigViewImpl(const std::string& yamlStr);
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
    // 获取节点（每次从字符串重新解析）
    YAML::Node getNode(const std::string& key) const;

    // 分割点分路径
    std::vector<std::string> splitPath(const std::string& key) const;

    // YAML 字符串（不可变数据源）
    const std::string m_yamlStr;
};

} // namespace config
} // namespace hwyz