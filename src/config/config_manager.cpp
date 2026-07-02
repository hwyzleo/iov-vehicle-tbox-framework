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

    ConfigError load(const std::string& serviceName, const std::string& configRoot) {
        // 重置状态
        m_loaded = false;
        m_snapshot.reset();

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

        // 5. 先序列化合并结果（validator.validate() 可能破坏 YAML::Node 内部引用）
        std::string mergedStr = YAML::Dump(merged);

        // 6. 校验
        ConfigValidator validator;
        validator.addRule({"log", ConfigType::kMap, true, "Log configuration"});

        ConfigErrorInfo validationError = validator.validate(merged);
        if (validationError.code != ConfigError::kOk) {
            m_lastError = validationError;
            return m_lastError.code;
        }

        // 7. 创建不可变快照（从序列化字符串重建，避免引用问题）
        YAML::Node mergedCopy = YAML::Load(mergedStr);
        m_snapshot = std::make_shared<ImmutableConfigViewImpl>(mergedCopy);
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
    : m_impl(new Impl())
{
}

ConfigManager::~ConfigManager() = default;

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
