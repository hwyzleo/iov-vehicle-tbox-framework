#pragma once

#include "store_types.h"
#include <string>
#include <map>
#include <chrono>
#include <mutex>
#include <vector>

namespace hwyz {
namespace store {

class FlushPolicy {
public:
    FlushPolicy(const FlushPolicyConfig& config = FlushPolicyConfig());
    ~FlushPolicy() = default;

    // 标记key为脏
    void markDirty(const std::string& key);

    // 清除脏标记
    void clearDirty(const std::string& key);

    // 检查key是否脏
    bool isDirty(const std::string& key) const;

    // 检查是否应该刷新
    bool shouldFlush() const;

    // 获取所有脏key
    std::vector<std::string> getDirtyKeys() const;

    // 重置策略
    void reset();

private:
    FlushPolicyConfig m_config;
    mutable std::mutex m_mutex;

    // 脏条目跟踪
    struct DirtyEntry {
        bool dirty;
        std::chrono::steady_clock::time_point lastDirtyTime;
    };
    std::map<std::string, DirtyEntry> m_dirtyEntries;

    // 检查去抖时间
    bool checkDebounce(const DirtyEntry& entry) const;
};

} // namespace store
} // namespace hwyz
