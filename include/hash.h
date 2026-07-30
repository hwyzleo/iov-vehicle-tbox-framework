#pragma once

#include "hash_types.h"

#include <string>
#include <string_view>

namespace tbox {
namespace fw {
namespace hash {

// ============================================================
// 一次性 SHA-256 摘要 API
// ============================================================

// 计算 SHA-256 原始摘要（32 字节）。
// 异常：ByteView 非零长度但 data 为空指针或 bit 长度换算溢出 -> HashException(FW-0401)。
Sha256Digest sha256(ByteView input);

// 计算 SHA-256 并返回 64 字符小写 Hex。
// 输出固定为 64 个 [0-9a-f] 字符，无前缀、分隔符、空白或换行。
std::string sha256_hex(ByteView input);

// std::string_view 便捷重载：按完整字节长度处理，内部 NUL 不截断，不执行字符集转换。
inline Sha256Digest sha256(std::string_view input) {
    return sha256(ByteView{
        reinterpret_cast<const std::uint8_t*>(input.data()), input.size()});
}

inline std::string sha256_hex(std::string_view input) {
    return sha256_hex(ByteView{
        reinterpret_cast<const std::uint8_t*>(input.data()), input.size()});
}

} // namespace hash
} // namespace fw
} // namespace tbox
