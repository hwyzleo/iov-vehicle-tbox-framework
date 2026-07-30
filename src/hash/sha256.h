// src/hash/sha256.h
//
// SHA-256 Core 内部实现（FIPS 180-4），不进入公共头文件。
// 首版只对外提供一次性 API（include/hash.h），该 Context 仅在 .cpp 内部使用。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace tbox {
namespace fw {
namespace hash {
namespace detail {

// 内部 SHA-256 上下文，组织 update/final；首版不暴露流式公共 API。
// 所有状态位于调用栈或调用私有对象中，无全局可变缓冲区，可重入。
class Sha256Context {
public:
    Sha256Context();

    // 追加输入字节；size==0 时为空操作，不解引用 data。
    void update(const std::uint8_t* data, std::size_t size);

    // 完成 padding 并输出 32 字节摘要（大端顺序）。
    std::array<std::uint8_t, 32> finalize();

private:
    void processBlock(const std::uint8_t* block);

    std::uint32_t m_state[8];   // H0..H7
    std::uint8_t  m_buffer[64]; // 当前未满块
    std::uint64_t m_bitlen;     // 已处理的总 bit 长度
    std::size_t   m_buffer_len; // m_buffer 中已有字节数
};

} // namespace detail
} // namespace hash
} // namespace fw
} // namespace tbox
