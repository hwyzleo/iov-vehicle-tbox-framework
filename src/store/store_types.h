#pragma once

#include <store_types.h>
#include <functional>

namespace hwyz {
namespace store {

// 序列化函数类型
using SerializerFunc = std::function<std::string(const void* data, size_t size)>;
using DeserializerFunc = std::function<bool(const std::string& bytes, void* data, size_t size)>;

// 刷新策略配置
struct FlushPolicyConfig {
    bool enableDebounce = true;      // 启用去抖
    uint32_t debounceMs = 1000;      // 去抖时间(ms)
    uint32_t maxDirtyCount = 10;     // 最大脏条目数
    bool enableBatch = true;         // 启用批量提交
};

} // namespace store
} // namespace hwyz
