#include "store_file_lock.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <chrono>
#include <thread>

namespace hwyz {
namespace store {

FileLock::FileLock(const PathResolver& pathResolver)
    : m_pathResolver(pathResolver)
{
}

FileLock::~FileLock() {
    for (auto& pair : m_lockFds) {
        releaseFileLock(pair.second);
    }
    m_lockFds.clear();
}

bool FileLock::acquire(const std::string& key, uint32_t timeoutMs) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_lockFds.find(key) != m_lockFds.end()) {
        return true;
    }

    std::string lockPath = getLockFilePath(key);
    int fd = -1;

    if (timeoutMs == 0) {
        if (!tryFileLock(lockPath, fd)) {
            return false;
        }
    } else {
        auto start = std::chrono::steady_clock::now();
        while (!tryFileLock(lockPath, fd)) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed >= timeoutMs) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    m_lockFds[key] = fd;

    return true;
}

void FileLock::release(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_lockFds.find(key);
    if (it == m_lockFds.end()) {
        return;
    }

    releaseFileLock(it->second);
    m_lockFds.erase(it);

    std::string lockPath = getLockFilePath(key);
    unlink(lockPath.c_str());
}

bool FileLock::isHeld(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lockFds.find(key) != m_lockFds.end();
}

std::string FileLock::getLockFilePath(const std::string& key) const {
    return m_pathResolver.getKeyPath(key) + ".lock";
}

bool FileLock::tryFileLock(const std::string& lockPath, int& fd) {
    fd = open(lockPath.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        return false;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        fd = -1;
        return false;
    }

    return true;
}

void FileLock::releaseFileLock(int fd) {
    if (fd >= 0) {
        flock(fd, LOCK_UN);
        close(fd);
    }
}

} // namespace store
} // namespace hwyz
