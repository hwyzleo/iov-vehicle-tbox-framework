# Framework 配置管理能力实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 基于设计变更 TBOX-FW-DSN-CR-001，实现 TBOX framework 配置管理能力，提供统一的配置加载、合并、校验与访问机制。

**Architecture:** 进程内 C++ 静态库，包含 PathResolver（路径解析）、YamlParser（YAML 解析）、Merger（深合并）、Validator（校验）、ImmutableConfigView（不可变快照）五个核心组件。加载时序：路径解析 → 逐层读取 → 解析 → 深合并 → 校验 → 只读快照。

**Tech Stack:** C++11, yaml-cpp (已集成), CMake

---

## 文件结构

### 新增文件
- `include/config.h` - 配置管理公共 API 头文件
- `include/config_types.h` - 配置类型定义（错误码、配置项类型等）
- `src/config/config_manager.cpp` - ConfigManager 实现
- `src/config/path_resolver.cpp` - 路径解析器实现
- `src/config/yaml_parser.cpp` - YAML 解析器实现
- `src/config/config_merger.cpp` - 配置合并器实现
- `src/config/config_validator.cpp` - 配置校验器实现
- `src/config/immutable_config_view.cpp` - 不可变配置快照实现
- `tests/test_config_manager.cpp` - 配置管理器单元测试
- `tests/test_path_resolver.cpp` - 路径解析器单元测试
- `tests/test_config_merger.cpp` - 配置合并器单元测试
- `tests/test_config_validator.cpp` - 配置校验器单元测试

### 修改文件
- `CMakeLists.txt` - 添加配置管理源文件和测试目标

---

## Task 1: 定义配置类型和错误码

**Files:**
- Create: `include/config_types.h`

- [ ] **Step 1: 创建配置类型定义头文件**

```cpp
#pragma once

#include <string>
#include <cstdint>

namespace hwyz {
namespace config {

// 错误码定义 (FW-0001~0004)
enum class ConfigError : uint32_t {
    kOk = 0,
    kFileNotFound = 1,      // FW-0001: 配置文件不存在
    kParseFailed = 2,       // FW-0002: YAML 解析失败
    kValidationFailed = 3,  // FW-0003: 校验失败（类型不匹配或必填项缺失）
    kMergeConflict = 4      // FW-0004: 合并冲突
};

// 配置项类型
enum class ConfigType : uint8_t {
    kScalar,   // 标量值（string, int, double, bool）
    kSequence, // 序列（数组）
    kMap       // 映射（对象）
};

// 配置层级
enum class ConfigLayer : uint8_t {
    kCommon = 0,    // common.yaml（必需）
    kService = 1,   // conf.d/<svc>.yaml（可选）
    kLocal = 2      // . /<svc>.yaml（可选）
};

// 错误信息结构
struct ConfigErrorInfo {
    ConfigError code;
    std::string message;
    std::string path;  // 发生错误的配置路径
};

} // namespace config
} // namespace hwyz
```

- [ ] **Step 2: 验证头文件编译**

Run: `cd /Users/hwyz_leo/Projects/open-iov/vehicle/tbox/iov-vehicle-tbox-framework && g++ -std=c++11 -I include -c include/config_types.h -o /dev/null`
Expected: 编译成功，无错误

- [ ] **Step 3: Commit**

```bash
git add include/config_types.h
git commit -m "feat(config): add config types and error codes definition"
```

---

## Task 2: 定义配置管理公共 API

**Files:**
- Create: `include/config.h`

- [ ] **Step 1: 创建配置管理公共 API 头文件**

```cpp
#pragma once

#include "config_types.h"
#include <string>
#include <memory>
#include <functional>

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
```

- [ ] **Step 2: 验证头文件编译**

Run: `cd /Users/hwyz_leo/Projects/open-iov/vehicle/tbox/iov-vehicle-tbox-framework && g++ -std=c++11 -I include -c include/config.h -o /dev/null`
Expected: 编译成功，无错误

- [ ] **Step 3: Commit**

```bash
git add include/config.h
git commit -m "feat(config): add config manager public API header"
```

---

## Task 3: 实现路径解析器

**Files:**
- Create: `src/config/path_resolver.h`
- Create: `src/config/path_resolver.cpp`

- [ ] **Step 1: 创建路径解析器头文件**

```cpp
#pragma once

#include "config_types.h"
#include <string>
#include <vector>

namespace hwyz {
namespace config {

// 路径解析器：负责解析配置文件路径
class PathResolver {
public:
    PathResolver(const std::string& serviceName, const std::string& configRoot);
    ~PathResolver() = default;

    // 获取各层配置文件路径
    std::string getCommonPath() const;      // /etc/tbox/common.yaml
    std::string getServicePath() const;     // /etc/tbox/conf.d/<svc>.yaml
    std::string getLocalPath() const;       // ./<svc>.yaml

    // 检查文件是否存在
    bool commonExists() const;
    bool serviceExists() const;
    bool localExists() const;

    // 获取服务名称
    std::string getServiceName() const;

    // 获取配置根目录
    std::string getConfigRoot() const;

private:
    std::string m_serviceName;
    std::string m_configRoot;
};

} // namespace config
} // namespace hwyz
```

- [ ] **Step 2: 创建路径解析器实现文件**

```cpp
#include "path_resolver.h"
#include <sys/stat.h>

namespace hwyz {
namespace config {

PathResolver::PathResolver(const std::string& serviceName, const std::string& configRoot)
    : m_serviceName(serviceName)
    , m_configRoot(configRoot)
{
    // 确保配置根目录以斜杠结尾
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
```

- [ ] **Step 3: 创建路径解析器单元测试**

```cpp
#include "path_resolver.h"
#include <cassert>
#include <iostream>
#include <fstream>

using namespace hwyz::config;

void test_path_resolver_basic() {
    PathResolver resolver("prov", "/etc/tbox");

    assert(resolver.getCommonPath() == "/etc/tbox/common.yaml");
    assert(resolver.getServicePath() == "/etc/tbox/conf.d/prov.yaml");
    assert(resolver.getLocalPath() == "./prov.yaml");
    assert(resolver.getServiceName() == "prov");
    assert(resolver.getConfigRoot() == "/etc/tbox/");

    std::cout << "test_path_resolver_basic passed" << std::endl;
}

void test_path_resolver_trailing_slash() {
    PathResolver resolver("sec", "/etc/tbox/");

    assert(resolver.getCommonPath() == "/etc/tbox/common.yaml");
    assert(resolver.getServicePath() == "/etc/tbox/conf.d/sec.yaml");
    assert(resolver.getLocalPath() == "./sec.yaml");

    std::cout << "test_path_resolver_trailing_slash passed" << std::endl;
}

int main() {
    test_path_resolver_basic();
    test_path_resolver_trailing_slash();
    return 0;
}
```

- [ ] **Step 4: 编译并运行测试**

Run: `cd /Users/hwyz_leo/Projects/open-iov/vehicle/tbox/iov-vehicle-tbox-framework && g++ -std=c++11 -I include -I src/config src/config/path_resolver.cpp tests/test_path_resolver.cpp -o test_path_resolver && ./test_path_resolver`
Expected: 两个测试都通过

- [ ] **Step 5: Commit**

```bash
git add src/config/path_resolver.h src/config/path_resolver.cpp tests/test_path_resolver.cpp
git commit -m "feat(config): implement path resolver for config file paths"
```

---

## Task 4: 实现配置合并器

**Files:**
- Create: `src/config/config_merger.h`
- Create: `src/config/config_merger.cpp`

- [ ] **Step 1: 创建配置合并器头文件**

```cpp
#pragma once

#include "config_types.h"
#include <yaml-cpp/yaml.h>
#include <string>

namespace hwyz {
namespace config {

// 配置合并器：负责深合并多个 YAML 节点
class ConfigMerger {
public:
    ConfigMerger() = default;
    ~ConfigMerger() = default;

    // 深合并两个 YAML 节点
    // 规则：map 递归合并，scalar / array 后者整体覆盖
    // 返回合并后的节点
    YAML::Node merge(const YAML::Node& base, const YAML::Node& override);

    // 合并多个节点（按顺序）
    YAML::Node mergeMultiple(const std::vector<YAML::Node>& nodes);

private:
    // 递归合并 map
    void mergeMap(YAML::Node& base, const YAML::Node& override);

    // 检查是否为 map 类型
    bool isMap(const YAML::Node& node) const;
};

} // namespace config
} // namespace hwyz
```

- [ ] **Step 2: 创建配置合并器实现文件**

```cpp
#include "config_merger.h"

namespace hwyz {
namespace config {

YAML::Node ConfigMerger::merge(const YAML::Node& base, const YAML::Node& override) {
    // 如果 base 为空，直接返回 override
    if (!base || base.IsNull()) {
        return YAML::Clone(override);
    }

    // 如果 override 为空，返回 base
    if (!override || override.IsNull()) {
        return YAML::Clone(base);
    }

    // 如果两者都是 map，递归合并
    if (isMap(base) && isMap(override)) {
        YAML::Node result = YAML::Clone(base);
        mergeMap(result, override);
        return result;
    }

    // 否则，override 整体覆盖 base
    return YAML::Clone(override);
}

YAML::Node ConfigMerger::mergeMultiple(const std::vector<YAML::Node>& nodes) {
    if (nodes.empty()) {
        return YAML::Node(YAML::NodeType::Null);
    }

    YAML::Node result = YAML::Clone(nodes[0]);
    for (size_t i = 1; i < nodes.size(); ++i) {
        result = merge(result, nodes[i]);
    }
    return result;
}

void ConfigMerger::mergeMap(YAML::Node& base, const YAML::Node& override) {
    for (auto it = override.begin(); it != override.end(); ++it) {
        std::string key = it->first.as<std::string>();
        YAML::Node overrideValue = it->second;

        // 如果 base 中存在该 key
        if (base[key]) {
            YAML::Node baseValue = base[key];

            // 如果两者都是 map，递归合并
            if (isMap(baseValue) && isMap(overrideValue)) {
                mergeMap(baseValue, overrideValue);
            } else {
                // 否则，override 整体覆盖
                base[key] = YAML::Clone(overrideValue);
            }
        } else {
            // base 中不存在该 key，直接添加
            base[key] = YAML::Clone(overrideValue);
        }
    }
}

bool ConfigMerger::isMap(const YAML::Node& node) const {
    return node && node.IsMap();
}

} // namespace config
} // namespace hwyz
```

- [ ] **Step 3: 创建配置合并器单元测试**

```cpp
#include "config_merger.h"
#include <cassert>
#include <iostream>

using namespace hwyz::config;

void test_merge_scalar_override() {
    ConfigMerger merger;

    YAML::Node base;
    base["key"] = "value1";

    YAML::Node override;
    override["key"] = "value2";

    YAML::Node result = merger.merge(base, override);
    assert(result["key"].as<std::string>() == "value2");

    std::cout << "test_merge_scalar_override passed" << std::endl;
}

void test_merge_map_deep() {
    ConfigMerger merger;

    YAML::Node base;
    base["server"]["host"] = "localhost";
    base["server"]["port"] = 8080;

    YAML::Node override;
    override["server"]["port"] = 9090;
    override["server"]["timeout"] = 30;

    YAML::Node result = merger.merge(base, override);
    assert(result["server"]["host"].as<std::string>() == "localhost");
    assert(result["server"]["port"].as<int>() == 9090);
    assert(result["server"]["timeout"].as<int>() == 30);

    std::cout << "test_merge_map_deep passed" << std::endl;
}

void test_merge_array_override() {
    ConfigMerger merger;

    YAML::Node base;
    base["items"][0] = "item1";
    base["items"][1] = "item2";

    YAML::Node override;
    override["items"][0] = "item3";

    YAML::Node result = merger.merge(base, override);
    assert(result["items"].size() == 1);
    assert(result["items"][0].as<std::string>() == "item3");

    std::cout << "test_merge_array_override passed" << std::endl;
}

void test_merge_multiple() {
    ConfigMerger merger;

    YAML::Node node1;
    node1["key1"] = "value1";

    YAML::Node node2;
    node2["key2"] = "value2";

    YAML::Node node3;
    node3["key1"] = "override1";

    std::vector<YAML::Node> nodes = {node1, node2, node3};
    YAML::Node result = merger.mergeMultiple(nodes);

    assert(result["key1"].as<std::string>() == "override1");
    assert(result["key2"].as<std::string>() == "value2");

    std::cout << "test_merge_multiple passed" << std::endl;
}

int main() {
    test_merge_scalar_override();
    test_merge_map_deep();
    test_merge_array_override();
    test_merge_multiple();
    return 0;
}
```

- [ ] **Step 4: 编译并运行测试**

Run: `cd /Users/hwyz_leo/Projects/open-iov/vehicle/tbox/iov-vehicle-tbox-framework && g++ -std=c++11 -I include -I src/config -I third_party/include src/config/config_merger.cpp tests/test_config_merger.cpp -L third_party/lib/$(uname -m)-$(uname -s | tr '[:upper:]' '[:lower:]')/yaml-cpp -lyaml-cpp -o test_config_merger && ./test_config_merger`
Expected: 所有测试都通过

- [ ] **Step 5: Commit**

```bash
git add src/config/config_merger.h src/config/config_merger.cpp tests/test_config_merger.cpp
git commit -m "feat(config): implement config merger with deep merge logic"
```

---

## Task 5: 实现配置校验器

**Files:**
- Create: `src/config/config_validator.h`
- Create: `src/config/config_validator.cpp`

- [ ] **Step 1: 创建配置校验器头文件**

```cpp
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
```

- [ ] **Step 2: 创建配置校验器实现文件**

```cpp
#include "config_validator.h"
#include <sstream>

namespace hwyz {
namespace config {

void ConfigValidator::addRule(const ValidationRule& rule) {
    m_rules.push_back(rule);
}

void ConfigValidator::addRules(const std::vector<ValidationRule>& rules) {
    m_rules.insert(m_rules.end(), rules.begin(), rules.end());
}

ConfigErrorInfo ConfigValidator::validate(const YAML::Node& config) const {
    for (const auto& rule : m_rules) {
        // 检查必填项
        if (rule.required && !nodeExists(config, rule.path)) {
            return {ConfigError::kValidationFailed,
                    "Required field missing: " + rule.description,
                    rule.path};
        }

        // 如果节点存在，检查类型
        if (nodeExists(config, rule.path)) {
            YAML::Node node = getNode(config, rule.path);
            if (!checkType(node, rule.expectedType)) {
                return {ConfigError::kValidationFailed,
                        "Type mismatch for field: " + rule.description,
                        rule.path};
            }
        }
    }

    return {ConfigError::kOk, "", ""};
}

void ConfigValidator::clearRules() {
    m_rules.clear();
}

bool ConfigValidator::nodeExists(const YAML::Node& config, const std::string& path) const {
    std::vector<std::string> parts = splitPath(path);
    YAML::Node current = config;

    for (const auto& part : parts) {
        if (!current || !current.IsMap() || !current[part]) {
            return false;
        }
        current = current[part];
    }

    return true;
}

YAML::Node ConfigValidator::getNode(const YAML::Node& config, const std::string& path) const {
    std::vector<std::string> parts = splitPath(path);
    YAML::Node current = config;

    for (const auto& part : parts) {
        if (!current || !current.IsMap()) {
            return YAML::Node();
        }
        current = current[part];
    }

    return current;
}

bool ConfigValidator::checkType(const YAML::Node& node, ConfigType expectedType) const {
    if (!node) {
        return false;
    }

    switch (expectedType) {
        case ConfigType::kScalar:
            return node.IsScalar();
        case ConfigType::kSequence:
            return node.IsSequence();
        case ConfigType::kMap:
            return node.IsMap();
        default:
            return false;
    }
}

std::vector<std::string> ConfigValidator::splitPath(const std::string& path) const {
    std::vector<std::string> parts;
    std::istringstream stream(path);
    std::string part;

    while (std::getline(stream, part, '.')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    return parts;
}

} // namespace config
} // namespace hwyz
```

- [ ] **Step 3: 创建配置校验器单元测试**

```cpp
#include "config_validator.h"
#include <cassert>
#include <iostream>

using namespace hwyz::config;

void test_validate_required_field() {
    ConfigValidator validator;
    validator.addRule({"log.level", ConfigType::kScalar, true, "Log level"});

    YAML::Node config;
    config["log"]["level"] = "info";

    ConfigErrorInfo error = validator.validate(config);
    assert(error.code == ConfigError::kOk);

    std::cout << "test_validate_required_field passed" << std::endl;
}

void test_validate_missing_required() {
    ConfigValidator validator;
    validator.addRule({"log.level", ConfigType::kScalar, true, "Log level"});

    YAML::Node config;
    config["log"]["format"] = "json";

    ConfigErrorInfo error = validator.validate(config);
    assert(error.code == ConfigError::kValidationFailed);
    assert(error.path == "log.level");

    std::cout << "test_validate_missing_required passed" << std::endl;
}

void test_validate_type_mismatch() {
    ConfigValidator validator;
    validator.addRule({"server.port", ConfigType::kScalar, true, "Server port"});

    YAML::Node config;
    config["server"]["port"][0] = 8080;
    config["server"]["port"][1] = 9090;

    ConfigErrorInfo error = validator.validate(config);
    assert(error.code == ConfigError::kValidationFailed);

    std::cout << "test_validate_type_mismatch passed" << std::endl;
}

void test_validate_optional_field() {
    ConfigValidator validator;
    validator.addRule({"log.file", ConfigType::kScalar, false, "Log file path"});

    YAML::Node config;
    config["log"]["level"] = "info";

    ConfigErrorInfo error = validator.validate(config);
    assert(error.code == ConfigError::kOk);

    std::cout << "test_validate_optional_field passed" << std::endl;
}

int main() {
    test_validate_required_field();
    test_validate_missing_required();
    test_validate_type_mismatch();
    test_validate_optional_field();
    return 0;
}
```

- [ ] **Step 4: 编译并运行测试**

Run: `cd /Users/hwyz_leo/Projects/open-iov/vehicle/tbox/iov-vehicle-tbox-framework && g++ -std=c++11 -I include -I src/config -I third_party/include src/config/config_validator.cpp tests/test_config_validator.cpp -L third_party/lib/$(uname -m)-$(uname -s | tr '[:upper:]' '[:lower:]')/yaml-cpp -lyaml-cpp -o test_config_validator && ./test_config_validator`
Expected: 所有测试都通过

- [ ] **Step 5: Commit**

```bash
git add src/config/config_validator.h src/config/config_validator.cpp tests/test_config_validator.cpp
git commit -m "feat(config): implement config validator with type checking"
```

---

## Task 6: 实现不可变配置快照

**Files:**
- Create: `src/config/immutable_config_view.h`
- Create: `src/config/immutable_config_view.cpp`

- [ ] **Step 1: 创建不可变配置快照头文件**

```cpp
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
```

- [ ] **Step 2: 创建不可变配置快照实现文件**

```cpp
#include "immutable_config_view.h"
#include <sstream>
#include <stdexcept>

namespace hwyz {
namespace config {

ImmutableConfigViewImpl::ImmutableConfigViewImpl(const YAML::Node& root)
    : m_root(YAML::Clone(root))  // 深拷贝确保不可变
{
}

bool ImmutableConfigViewImpl::has(const std::string& key) const {
    try {
        YAML::Node node = getNode(key);
        return node && !node.IsNull();
    } catch (...) {
        return false;
    }
}

std::string ImmutableConfigViewImpl::getString(const std::string& key, const std::string& defaultValue) const {
    try {
        YAML::Node node = getNode(key);
        if (node && node.IsScalar()) {
            return node.as<std::string>();
        }
    } catch (...) {
        // 忽略异常
    }
    return defaultValue;
}

int ImmutableConfigViewImpl::getInt(const std::string& key, int defaultValue) const {
    try {
        YAML::Node node = getNode(key);
        if (node && node.IsScalar()) {
            return node.as<int>();
        }
    } catch (...) {
        // 忽略异常
    }
    return defaultValue;
}

double ImmutableConfigViewImpl::getDouble(const std::string& key, double defaultValue) const {
    try {
        YAML::Node node = getNode(key);
        if (node && node.IsScalar()) {
            return node.as<double>();
        }
    } catch (...) {
        // 忽略异常
    }
    return defaultValue;
}

bool ImmutableConfigViewImpl::getBool(const std::string& key, bool defaultValue) const {
    try {
        YAML::Node node = getNode(key);
        if (node && node.IsScalar()) {
            return node.as<bool>();
        }
    } catch (...) {
        // 忽略异常
    }
    return defaultValue;
}

std::vector<std::string> ImmutableConfigViewImpl::getStringList(const std::string& key) const {
    std::vector<std::string> result;
    try {
        YAML::Node node = getNode(key);
        if (node && node.IsSequence()) {
            for (const auto& item : node) {
                if (item.IsScalar()) {
                    result.push_back(item.as<std::string>());
                }
            }
        }
    } catch (...) {
        // 忽略异常
    }
    return result;
}

std::shared_ptr<const ImmutableConfigView> ImmutableConfigViewImpl::getSection(const std::string& key) const {
    try {
        YAML::Node node = getNode(key);
        if (node && node.IsMap()) {
            return std::make_shared<ImmutableConfigViewImpl>(node);
        }
    } catch (...) {
        // 忽略异常
    }
    return nullptr;
}

std::vector<std::string> ImmutableConfigViewImpl::getKeys() const {
    std::vector<std::string> keys;
    if (m_root && m_root.IsMap()) {
        for (auto it = m_root.begin(); it != m_root.end(); ++it) {
            keys.push_back(it->first.as<std::string>());
        }
    }
    return keys;
}

YAML::Node ImmutableConfigViewImpl::getNode(const std::string& key) const {
    std::vector<std::string> parts = splitPath(key);
    YAML::Node current = m_root;

    for (const auto& part : parts) {
        if (!current || !current.IsMap()) {
            return YAML::Node();
        }
        current = current[part];
    }

    return current;
}

std::vector<std::string> ImmutableConfigViewImpl::splitPath(const std::string& key) const {
    std::vector<std::string> parts;
    std::istringstream stream(key);
    std::string part;

    while (std::getline(stream, part, '.')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    return parts;
}

} // namespace config
} // namespace hwyz
```

- [ ] **Step 3: Commit**

```bash
git add src/config/immutable_config_view.h src/config/immutable_config_view.cpp
git commit -m "feat(config): implement immutable config view with thread-safe access"
```

---

## Task 7: 实现配置管理器核心

**Files:**
- Create: `src/config/config_manager.cpp`

- [ ] **Step 1: 创建配置管理器实现文件**

```cpp
#include "config.h"
#include "path_resolver.h"
#include "config_merger.h"
#include "config_validator.h"
#include "immutable_config_view.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <iostream>

namespace hwyz {
namespace config {

// 配置管理器内部实现
class ConfigManager::Impl {
public:
    Impl() = default;
    ~Impl() = default;

    ConfigError load(const std::string& serviceName, const std::string& configRoot) {
        // 1. 路径解析
        PathResolver resolver(serviceName, configRoot);

        // 2. 检查必需层（common.yaml）
        if (!resolver.commonExists()) {
            m_lastError = {ConfigError::kFileNotFound,
                          "Required config file not found: " + resolver.getCommonPath(),
                          "common.yaml"};
            return m_lastError.code;
        }

        // 3. 逐层读取和解析
        std::vector<YAML::Node> layers;

        // 读取 common.yaml
        try {
            YAML::Node common = YAML::LoadFile(resolver.getCommonPath());
            layers.push_back(common);
        } catch (const YAML::Exception& e) {
            m_lastError = {ConfigError::kParseFailed,
                          "Failed to parse common.yaml: " + std::string(e.what()),
                          "common.yaml"};
            return m_lastError.code;
        }

        // 读取 conf.d/<svc>.yaml（可选）
        if (resolver.serviceExists()) {
            try {
                YAML::Node service = YAML::LoadFile(resolver.getServicePath());
                layers.push_back(service);
            } catch (const YAML::Exception& e) {
                m_lastError = {ConfigError::kParseFailed,
                              "Failed to parse service config: " + std::string(e.what()),
                              resolver.getServicePath()};
                return m_lastError.code;
            }
        }

        // 读取 ./<svc>.yaml（可选）
        if (resolver.localExists()) {
            try {
                YAML::Node local = YAML::LoadFile(resolver.getLocalPath());
                layers.push_back(local);
            } catch (const YAML::Exception& e) {
                m_lastError = {ConfigError::kParseFailed,
                              "Failed to parse local config: " + std::string(e.what()),
                              resolver.getLocalPath()};
                return m_lastError.code;
            }
        }

        // 4. 深合并
        ConfigMerger merger;
        YAML::Node merged = merger.mergeMultiple(layers);

        // 5. 校验
        ConfigValidator validator;
        // 添加基本校验规则（可根据实际需求扩展）
        validator.addRule({"log", ConfigType::kMap, true, "Log configuration"});

        ConfigErrorInfo validationError = validator.validate(merged);
        if (validationError.code != ConfigError::kOk) {
            m_lastError = validationError;
            return m_lastError.code;
        }

        // 6. 创建不可变快照
        m_snapshot = std::make_shared<ImmutableConfigViewImpl>(merged);
        m_loaded = true;

        m_lastError = {ConfigError::kOk, "", ""};
        return ConfigError::kOk;
    }

    std::shared_ptr<const ImmutableConfigView> getSnapshot() const {
        return m_snapshot;
    }

    bool isLoaded() const {
        return m_loaded;
    }

    ConfigErrorInfo getLastError() const {
        return m_lastError;
    }

private:
    std::shared_ptr<const ImmutableConfigView> m_snapshot;
    bool m_loaded = false;
    ConfigErrorInfo m_lastError = {ConfigError::kOk, "", ""};
};

// ConfigManager 单例实现
ConfigManager& ConfigManager::instance() {
    static ConfigManager instance;
    return instance;
}

ConfigManager::ConfigManager()
    : m_impl(std::make_unique<Impl>())
{
}

ConfigError ConfigManager::load(const std::string& serviceName, const std::string& configRoot) {
    return m_impl->load(serviceName, configRoot);
}

std::shared_ptr<const ImmutableConfigView> ConfigManager::getSnapshot() const {
    return m_impl->getSnapshot();
}

bool ConfigManager::isLoaded() const {
    return m_impl->isLoaded();
}

ConfigErrorInfo ConfigManager::getLastError() const {
    return m_impl->getLastError();
}

} // namespace config
} // namespace hwyz
```

- [ ] **Step 2: Commit**

```bash
git add src/config/config_manager.cpp
git commit -m "feat(config): implement config manager core with loading sequence"
```

---

## Task 8: 创建配置管理器单元测试

**Files:**
- Create: `tests/test_config_manager.cpp`

- [ ] **Step 1: 创建配置管理器单元测试**

```cpp
#include "config.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdio>

using namespace hwyz::config;

// 创建临时配置文件
void createTempFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
    file.close();
}

// 清理临时文件
void removeTempFile(const std::string& path) {
    std::remove(path.c_str());
}

void test_load_basic() {
    // 创建临时目录和文件
    std::string tempDir = "/tmp/test_config_";
    std::string commonPath = tempDir + "common.yaml";
    std::string serviceDir = tempDir + "conf.d/";
    std::string servicePath = serviceDir + "test.yaml";

    // 创建 common.yaml
    createTempFile(commonPath, R"(
log:
  level: info
  format: json
server:
  host: localhost
  port: 8080
)");

    // 创建 conf.d 目录和服务配置
    system(("mkdir -p " + serviceDir).c_str());
    createTempFile(servicePath, R"(
server:
  port: 9090
)");

    // 加载配置
    ConfigManager& manager = ConfigManager::instance();
    ConfigError error = manager.load("test", tempDir);

    assert(error == ConfigError::kOk);
    assert(manager.isLoaded());

    // 获取快照
    auto snapshot = manager.getSnapshot();
    assert(snapshot != nullptr);

    // 验证合并结果
    assert(snapshot->getString("log.level") == "info");
    assert(snapshot->getString("log.format") == "json");
    assert(snapshot->getString("server.host") == "localhost");
    assert(snapshot->getInt("server.port") == 9090);  // 被服务配置覆盖

    // 清理
    removeTempFile(commonPath);
    removeTempFile(servicePath);
    system(("rmdir " + serviceDir).c_str());
    system(("rmdir " + tempDir).c_str());

    std::cout << "test_load_basic passed" << std::endl;
}

void test_load_missing_common() {
    ConfigManager& manager = ConfigManager::instance();
    ConfigError error = manager.load("test", "/nonexistent/path/");

    assert(error == ConfigError::kFileNotFound);
    assert(!manager.isLoaded());

    std::cout << "test_load_missing_common passed" << std::endl;
}

void test_snapshot_operations() {
    // 创建临时配置文件
    std::string tempDir = "/tmp/test_config_snap_";
    std::string commonPath = tempDir + "common.yaml";

    createTempFile(commonPath, R"(
app:
  name: test-app
  version: 1.0
  debug: true
  tags:
    - tag1
    - tag2
database:
  host: localhost
  port: 5432
)");

    system(("mkdir -p " + tempDir).c_str());

    // 加载配置
    ConfigManager& manager = ConfigManager::instance();
    manager.load("test", tempDir);

    auto snapshot = manager.getSnapshot();

    // 测试各种获取方法
    assert(snapshot->has("app.name"));
    assert(!snapshot->has("app.nonexistent"));
    assert(snapshot->getString("app.name") == "test-app");
    assert(snapshot->getDouble("app.version") == 1.0);
    assert(snapshot->getBool("app.debug") == true);
    assert(snapshot->getInt("database.port") == 5432);

    // 测试列表
    auto tags = snapshot->getStringList("app.tags");
    assert(tags.size() == 2);
    assert(tags[0] == "tag1");
    assert(tags[1] == "tag2");

    // 测试嵌套 section
    auto dbSection = snapshot->getSection("database");
    assert(dbSection != nullptr);
    assert(dbSection->getString("host") == "localhost");
    assert(dbSection->getInt("port") == 5432);

    // 测试获取所有键
    auto keys = snapshot->getKeys();
    assert(keys.size() == 2);  // app, database

    // 清理
    removeTempFile(commonPath);
    system(("rmdir " + tempDir).c_str());

    std::cout << "test_snapshot_operations passed" << std::endl;
}

int main() {
    test_load_basic();
    test_load_missing_common();
    test_snapshot_operations();
    return 0;
}
```

- [ ] **Step 2: 编译并运行测试**

Run: `cd /Users/hwyz_leo/Projects/open-iov/vehicle/tbox/iov-vehicle-tbox-framework && g++ -std=c++11 -I include -I src/config -I third_party/include src/config/*.cpp tests/test_config_manager.cpp -L third_party/lib/$(uname -m)-$(uname -s | tr '[:upper:]' '[:lower:]')/yaml-cpp -lyaml-cpp -lpthread -o test_config_manager && ./test_config_manager`
Expected: 所有测试都通过

- [ ] **Step 3: Commit**

```bash
git add tests/test_config_manager.cpp
git commit -m "test(config): add unit tests for config manager"
```

---

## Task 9: 更新 CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 更新 CMakeLists.txt 添加配置管理源文件**

在 `CMakeLists.txt` 中添加以下内容：

```cmake
# 配置管理源文件
set(CONFIG_SOURCES
    src/config/config_manager.cpp
    src/config/path_resolver.cpp
    src/config/config_merger.cpp
    src/config/config_validator.cpp
    src/config/immutable_config_view.cpp
)

set(CONFIG_HEADERS
    include/config.h
    include/config_types.h
    src/config/path_resolver.h
    src/config/config_merger.h
    src/config/config_validator.h
    src/config/immutable_config_view.h
)

# 更新源文件收集
file(GLOB_RECURSE FRAMEWORK_SOURCES src/*.cpp)
file(GLOB_RECURSE FRAMEWORK_HEADERS include/*.h)

# 添加测试目标
add_executable(test_config_manager
    tests/test_config_manager.cpp
    ${CONFIG_SOURCES}
)

target_include_directories(test_config_manager
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/include
)

target_link_libraries(test_config_manager
    PRIVATE
    yaml-cpp
    pthread
)

# 添加其他测试目标
add_executable(test_path_resolver
    tests/test_path_resolver.cpp
    src/config/path_resolver.cpp
)

target_include_directories(test_path_resolver
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config
)

add_executable(test_config_merger
    tests/test_config_merger.cpp
    src/config/config_merger.cpp
)

target_include_directories(test_config_merger
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/include
)

target_link_libraries(test_config_merger
    PRIVATE
    yaml-cpp
)

add_executable(test_config_validator
    tests/test_config_validator.cpp
    src/config/config_validator.cpp
)

target_include_directories(test_config_validator
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/include
)

target_link_libraries(test_config_validator
    PRIVATE
    yaml-cpp
)
```

- [ ] **Step 2: 验证 CMake 配置**

Run: `cd /Users/hwyz_leo/Projects/open-iov/vehicle/tbox/iov-vehicle-tbox-framework && mkdir -p build && cd build && cmake .. && make test_config_manager test_path_resolver test_config_merger test_config_validator`
Expected: 所有测试目标编译成功

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(config): update CMakeLists.txt with config management sources and tests"
```

---

## Task 10: 集成测试和验证

- [ ] **Step 1: 运行所有测试**

Run: `cd /Users/hwyz_leo/Projects/open-iov/vehicle/tbox/iov-vehicle-tbox-framework/build && ./test_path_resolver && ./test_config_merger && ./test_config_validator && ./test_config_manager`
Expected: 所有测试都通过

- [ ] **Step 2: 验证库编译**

Run: `cd /Users/hwyz_leo/Projects/open-iov/vehicle/tbox/iov-vehicle-tbox-framework/build && make hwyz`
Expected: 库编译成功，包含配置管理功能

- [ ] **Step 3: 更新 graphify 知识图谱**

Run: `cd /Users/hwyz_leo/Projects/open-iov/vehicle/tbox/iov-vehicle-tbox-framework && graphify update .`
Expected: 知识图谱更新成功

- [ ] **Step 4: 最终 Commit**

```bash
git add -A
git commit -m "feat(config): complete framework configuration management implementation

- Implement ConfigManager with singleton pattern
- Implement PathResolver for config file path resolution
- Implement ConfigMerger with deep merge logic
- Implement ConfigValidator with type checking
- Implement ImmutableConfigView for thread-safe read-only access
- Add comprehensive unit tests
- Update CMakeLists.txt with new sources and test targets

Based on design change TBOX-FW-DSN-CR-001 and requirement TBOX-FW-REQ-CR-001"
```

---

## 执行建议

**推荐方式：Subagent-Driven Development**
- 每个 Task 分发给独立的 subagent 执行
- Task 之间进行 review，确保质量
- 快速迭代，及时发现问题

**备选方式：Inline Execution**
- 在当前会话中按顺序执行所有 Task
- 每个 Task 完成后进行检查点 review
- 批量执行，提高效率

---

## 注意事项

1. **不破坏现有功能**：所有新增代码都是独立的配置管理模块，不影响现有的 application.h、utils.h 等文件
2. **向后兼容**：现有的 yaml-cpp 依赖已经存在，无需额外添加
3. **线程安全**：ImmutableConfigView 设计为只读且线程安全
4. **错误处理**：完善的错误码和错误信息，便于调试
5. **可扩展性**：校验规则可扩展，支持未来添加更多校验逻辑
