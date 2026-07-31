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

    // CR-008: 从合并后的完整 YAML 文本重新解析，返回独立完整树。
    // 该方法为受控内部接口（非公共虚函数），不暴露给 framework-config 消费方。
    // 每次调用产生独立 YAML::Node，天然与内部快照隔离，且无损保留所有节点类型
    // （scalar / map / sequence / null / 任意嵌套），替代基于 getter 的有损反向拼树。
    YAML::Node toYamlClone() const;

private:
    // 获取节点
    YAML::Node getNode(const std::string& key) const;

    // 分割点分路径
    std::vector<std::string> splitPath(const std::string& key) const;

    // YAML 字符串（用于重新解析）
    const std::string m_yamlStr;
};

} // namespace config
} // namespace hwyz
