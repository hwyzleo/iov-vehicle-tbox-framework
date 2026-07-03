#include "store_flush_policy.h"

namespace hwyz {
namespace store {

FlushPolicy::FlushPolicy(const FlushPolicyConfig& config)
    : m_config(config)
{
}

void FlushPolicy::markDirty(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto& entry = m_dirtyEntries[key];
    entry.dirty = true;
    entry.lastDirtyTime = std::chrono::steady_clock::now();
}

void FlushPolicy::clearDirty(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_dirtyEntries.find(key);
    if (it != m_dirtyEntries.end()) {
        it->second.dirty = false;
    }
}

bool FlushPolicy::isDirty(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_dirtyEntries.find(key);
    if (it != m_dirtyEntries.end()) {
        return it->second.dirty;
    }
    return false;
}

bool FlushPolicy::shouldFlush() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 检查是否有脏条目
    if (m_dirtyEntries.empty()) {
        return false;
    }

    // 统计脏条目数量
    size_t dirtyCount = 0;
    for (const auto& pair : m_dirtyEntries) {
        if (pair.second.dirty) {
            dirtyCount++;
        }
    }

    // 检查是否达到阈值
    if (dirtyCount >= m_config.maxDirtyCount) {
        return true;
    }

    // 检查去抖
    if (m_config.enableDebounce) {
        for (const auto& pair : m_dirtyEntries) {
            if (pair.second.dirty && checkDebounce(pair.second)) {
                return true;
            }
        }
    }

    return false;
}

std::vector<std::string> FlushPolicy::getDirtyKeys() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<std::string> keys;
    for (const auto& pair : m_dirtyEntries) {
        if (pair.second.dirty) {
            keys.push_back(pair.first);
        }
    }
    return keys;
}

void FlushPolicy::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dirtyEntries.clear();
}

bool FlushPolicy::checkDebounce(const DirtyEntry& entry) const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - entry.lastDirtyTime).count();

    return (elapsed >= m_config.debounceMs);
}

} // namespace store
} // namespace hwyz
