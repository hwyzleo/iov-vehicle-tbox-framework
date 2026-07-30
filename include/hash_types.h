#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace tbox {
namespace fw {
namespace hash {

// ============================================================
// 错误码定义 (FW-0401~0402)
// ============================================================
enum class HashError : uint32_t {
    kOk = 0,
    kInvalidInput = 401,        // FW-0401: 非零长度空指针、长度换算溢出或输入范围非法
    kInternalFailure = 402      // FW-0402: 内部不变量、自检或后端计算失败
};

// 错误信息结构
struct HashErrorInfo {
    HashError code;
    std::string message;
    std::string detail;  // 附加上下文（输入长度等，不含原始输入）

    HashErrorInfo() : code(HashError::kOk) {}
    HashErrorInfo(HashError c, const std::string& m, const std::string& d = "")
        : code(c), message(m), detail(d) {}
};

// 哈希异常类
class HashException : public std::exception {
public:
    HashException(HashError code, const std::string& message, const std::string& detail = "")
        : m_error{code, message, detail} {}

    HashErrorInfo getError() const { return m_error; }
    const char* what() const noexcept override { return m_error.message.c_str(); }

private:
    HashErrorInfo m_error;
};

// ============================================================
// 输入视图与摘要类型
// ============================================================

// 二进制安全的字节视图；不依赖 C 字符串终止符。
// ByteView{nullptr, 0} 是合法空输入；size > 0 && data == nullptr 非法（FW-0401）。
struct ByteView {
    const std::uint8_t* data{nullptr};
    std::size_t size{0};
};

// SHA-256 原始摘要，固定 32 字节，字节顺序与标准摘要展示顺序一致。
using Sha256Digest = std::array<std::uint8_t, 32>;

} // namespace hash
} // namespace fw
} // namespace tbox
