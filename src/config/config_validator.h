#pragma once

#include "config_types.h"
#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>
#include <functional>

namespace hwyz {
namespace config {

// 校验规则
struct ValidationRule {
    std::string path;           // 配置路径（点分命名空间）
    ConfigType expectedType;    // 期望类型
    bool required;              // 是否必填
    std::string description;    // 描述（用于错误信息）
};

// 配置校验器：负责校验配置的有效性
class ConfigValidator {
public:
    ConfigValidator() = default;
    ~ConfigValidator() = default;

    // 添加校验规则
    void addRule(const ValidationRule& rule);

    // 批量添加规则
    void addRules(const std::vector<ValidationRule>& rules);

    // 校验配置
    // 返回第一个校验错误，如果全部通过返回 kOk
    ConfigErrorInfo validate(const YAML::Node& config) const;

    // 清空规则
    void clearRules();

private:
    // 检查节点是否存在
    bool nodeExists(const YAML::Node& config, const std::string& path) const;

    // 获取节点
    YAML::Node getNode(const YAML::Node& config, const std::string& path) const;

    // 检查节点类型
    bool checkType(const YAML::Node& node, ConfigType expectedType) const;

    // 分割点分路径
    std::vector<std::string> splitPath(const std::string& path) const;

    std::vector<ValidationRule> m_rules;
};

} // namespace config
} // namespace hwyz
