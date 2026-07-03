#pragma once

#include "store_path_resolver.h"
#include <string>
#include <mutex>
#include <map>
#include <atomic>

namespace hwyz {
namespace store {

class FileLock {
public:
    FileLock(const PathResolver& pathResolver);
    ~FileLock();

    bool acquire(const std::string& key, uint32_t timeoutMs = 0);
    void release(const std::string& key);
    bool isHeld(const std::string& key) const;

private:
    const PathResolver& m_pathResolver;

    mutable std::mutex m_mutex;
    std::map<std::string, std::atomic<bool>> m_heldLocks;

    std::string getLockFilePath(const std::string& key) const;
    bool tryFileLock(const std::string& lockPath, int& fd);
    void releaseFileLock(int fd);
};

} // namespace store
} // namespace hwyz
