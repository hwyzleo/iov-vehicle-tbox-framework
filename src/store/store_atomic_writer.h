#pragma once

#include "store_path_resolver.h"
#include <string>

namespace hwyz {
namespace store {

class AtomicWriter {
public:
    AtomicWriter(const PathResolver& pathResolver);
    ~AtomicWriter() = default;

    // 原子写入：写temp → fsync(temp) → rename → fsync(dir)
    // 失败时保留原文件
    bool write(const std::string& key, const std::string& data);

    // 检查key对应的文件是否存在
    bool exists(const std::string& key) const;

    // 删除key对应的文件
    bool remove(const std::string& key);

private:
    const PathResolver& m_pathResolver;

    // 写入临时文件
    bool writeTempFile(const std::string& tempPath, const std::string& data);

    // 同步文件到磁盘
    bool syncFile(const std::string& filePath);

    // 同步目录到磁盘
    bool syncDirectory(const std::string& dirPath);

    // 原子重命名
    bool atomicRename(const std::string& from, const std::string& to);
};

} // namespace store
} // namespace hwyz
