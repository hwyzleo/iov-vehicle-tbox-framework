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
