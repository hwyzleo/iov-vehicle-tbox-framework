#include "store_atomic_writer.h"
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdio>

namespace hwyz {
namespace store {

AtomicWriter::AtomicWriter(const PathResolver& pathResolver)
    : m_pathResolver(pathResolver)
{
}

bool AtomicWriter::write(const std::string& key, const std::string& data) {
    std::string tempPath = m_pathResolver.getTempPath(key);
    std::string finalPath = m_pathResolver.getKeyPath(key);

    // 1. 写入临时文件
    if (!writeTempFile(tempPath, data)) {
        return false;
    }

    // 2. 同步临时文件到磁盘
    if (!syncFile(tempPath)) {
        // 清理临时文件
        std::remove(tempPath.c_str());
        return false;
    }

    // 3. 原子重命名
    if (!atomicRename(tempPath, finalPath)) {
        // 清理临时文件
        std::remove(tempPath.c_str());
        return false;
    }

    // 4. 同步目录到磁盘
    if (!syncDirectory(m_pathResolver.getStorePath())) {
        // 重命名已成功，但目录未同步
        // 这种情况下文件已存在，只是可能在掉电后丢失
        return false;
    }

    return true;
}

bool AtomicWriter::exists(const std::string& key) const {
    std::string filePath = m_pathResolver.getKeyPath(key);
    struct stat st;
    return (stat(filePath.c_str(), &st) == 0);
}

bool AtomicWriter::remove(const std::string& key) {
    std::string filePath = m_pathResolver.getKeyPath(key);
    return (std::remove(filePath.c_str()) == 0);
}

bool AtomicWriter::writeTempFile(const std::string& tempPath, const std::string& data) {
    // 使用C风格文件操作，确保权限正确
    int fd = open(tempPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return false;
    }

    ssize_t written = ::write(fd, data.c_str(), data.size());
    close(fd);

    return (written == static_cast<ssize_t>(data.size()));
}

bool AtomicWriter::syncFile(const std::string& filePath) {
    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    int result = fsync(fd);
    close(fd);

    return (result == 0);
}

bool AtomicWriter::syncDirectory(const std::string& dirPath) {
    int fd = open(dirPath.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    int result = fsync(fd);
    close(fd);

    return (result == 0);
}

bool AtomicWriter::atomicRename(const std::string& from, const std::string& to) {
    return (rename(from.c_str(), to.c_str()) == 0);
}

} // namespace store
} // namespace hwyz
