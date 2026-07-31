#include "config.h"
#include "path_resolver.h"
#include "config_merger.h"
#include "config_validator.h"
#include "immutable_config_view.h"
#include <yaml-cpp/yaml.h>
#include <vector>

namespace hwyz {
namespace config {

// 配置管理器内部实现
class ConfigManager::Impl {
public:
    Impl() = default;
    ~Impl() = default;

    // 单根目录加载（向后兼容）
    ConfigError load(const std::string& serviceName, const std::string& configRoot) {
        return loadInternal(serviceName, std::vector<std::string>{configRoot});
    }

    // 多根目录加载
    ConfigError load(const std::string& serviceName, const std::vector<std::string>& configRoots) {
        return loadInternal(serviceName, configRoots);
    }

    YAML::Node toYaml() const {
        // CR-008: 以合并后的完整 YAML 表示为导出事实源，直接重新解析得到独立完整树，
        // 不再通过 getKeys/getSection/getString 反向拼树（后者无法完备表达 YAML AST，
        // 会丢失 sequence-of-maps、嵌套 sequence 等结构）。
        // 每次返回独立 YAML::Node，天然与内部不可变快照隔离，调用方修改不反向污染。
        if (!m_loaded || !m_snapshot) {
            return YAML::Node();
        }
        auto impl = std::dynamic_pointer_cast<const ImmutableConfigViewImpl>(m_snapshot);
        if (!impl) {
            return YAML::Node();
        }
        return impl->toYamlClone();
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
    ConfigError loadInternal(const std::string& serviceName,
                             const std::vector<std::string>& configRoots) {
        // 重置状态
        m_loaded = false;
        m_snapshot.reset();

        // 1. 路径解析（多根目录）
        PathResolver resolver(serviceName, configRoots);

        // 2. 按优先级收集所有存在的配置文件
        std::vector<ConfigEntry> entries = resolver.resolveAll();

        // 3. common.yaml（root 层）与 ./config/common.yaml（项目层）至少存在一个
        bool hasCommon = false;
        for (const auto& entry : entries) {
            if (entry.layer == "common" || entry.layer == "project/common") {
                hasCommon = true;
                break;
            }
        }
        if (!hasCommon) {
            m_lastError = {ConfigError::kFileNotFound,
                          "Required config file not found: common.yaml (root) or ./config/common.yaml (project)",
                          "common.yaml"};
            return m_lastError.code;
        }

        // 4. 逐层读取
        std::vector<YAML::Node> layers;
        for (const auto& entry : entries) {
            try {
                YAML::Node node = YAML::LoadFile(entry.path);
                layers.push_back(node);
            } catch (const YAML::Exception& e) {
                m_lastError = {ConfigError::kParseFailed,
                              "Failed to parse " + entry.layer + ": " + std::string(e.what()),
                              entry.path};
                return m_lastError.code;
            }
        }

        // 5. 深合并（后面的层覆盖前面的）
        ConfigMerger merger;
        YAML::Node merged = merger.mergeMultiple(layers);

        // 6. 先序列化合并结果（validator.validate() 可能破坏 YAML::Node 内部引用）
        std::string mergedStr = YAML::Dump(merged);

        // 7. 校验：日志配置必须位于 common.log
        //    （公共配置统一收敛到 common 命名空间下，见各服务 common.yaml）
        //    注意：ConfigValidator 内部按路径逐级取节点，会在缺失的键上留下
        //    zombie 节点，故校验用 mergedStr 重建一份独立的 Node。
        ConfigValidator validator;
        validator.addRule({"common.log", ConfigType::kMap, true, "Log configuration (common.log)"});

        YAML::Node validateNode = YAML::Load(mergedStr);
        ConfigErrorInfo validationError = validator.validate(validateNode);
        if (validationError.code != ConfigError::kOk) {
            m_lastError = validationError;
            return m_lastError.code;
        }

        // 8. 创建不可变快照（从序列化字符串重建，避免引用问题）
        YAML::Node mergedCopy = YAML::Load(mergedStr);
        m_snapshot = std::make_shared<ImmutableConfigViewImpl>(mergedCopy);
        m_loaded = true;

        m_lastError = {ConfigError::kOk, "", ""};
        return ConfigError::kOk;
    }

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
    : m_impl(new Impl())
{
}

ConfigManager::~ConfigManager() = default;

ConfigError ConfigManager::load(const std::string& serviceName, const std::string& configRoot) {
    return m_impl->load(serviceName, configRoot);
}

ConfigError ConfigManager::load(const std::string& serviceName,
                                const std::vector<std::string>& configRoots) {
    return m_impl->load(serviceName, configRoots);
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

YAML::Node ConfigManager::toYaml() const {
    return m_impl->toYaml();
}

} // namespace config
} // namespace hwyz
