#pragma once

#include <string>
#include <cstdint>

namespace hwyz {
namespace store {

// 错误码定义 (FW-0101~0105)
enum class StoreError : uint32_t {
    kOk = 0,
    kPathUnavailable = 101,    // FW-0101: 存储路径不可用
    kAtomicWriteFailed = 102,  // FW-0102: 原子写失败
    kLockFailed = 103,         // FW-0103: 并发锁获取失败/超时
    kSerializationFailed = 104,// FW-0104: 序列化/反序列化失败
    kKeyNotFound = 105         // FW-0105: 读取项缺失
};

// 错误信息结构
struct StoreErrorInfo {
    StoreError code;
    std::string message;
    std::string key;  // 发生错误的key
};

// 存储异常类
class StoreException : public std::exception {
public:
    StoreException(StoreError code, const std::string& message, const std::string& key = "")
        : m_error{code, message, key} {}

    StoreErrorInfo getError() const { return m_error; }
    const char* what() const noexcept override { return m_error.message.c_str(); }

private:
    StoreErrorInfo m_error;
};

} // namespace store
} // namespace hwyz
