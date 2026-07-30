// src/hash/sha256.cpp
//
// SHA-256 实现（FIPS 180-4），通用摘要能力 framework-hash。
//
// ----------------------------------------------------------------------------
// 实现来源与许可证说明
// ----------------------------------------------------------------------------
// 本文件为基于 FIPS 180-4 规范自研的最小 SHA-256 实现，无外部运行时依赖
// （不依赖 OpenSSL/mbedTLS 等密码库）。未复制任何第三方源码；算法常量与
// 处理流程严格对应 FIPS 180-4。
//
// FIPS 180-4 对应关系：
//   - 初始哈希值 H0..H7：FIPS 180-4 §5.3.3
//   - 轮常量 K0..K63：FIPS 180-4 §4.2.2
//   - 函数 Ch/Maj/Σ0/Σ1/σ0/σ1：FIPS 180-4 §4.1.2
//   - 消息调度与压缩：FIPS 180-4 §6.2.2
//   - Padding：FIPS 180-4 §5.1.1
//
// 本地修改记录：首版交付，无修改。
// 许可证：随项目整体许可证（见仓库根 LICENSE）。
// ----------------------------------------------------------------------------
#include "sha256.h"

#include "hash.h"
#include "hash_types.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace tbox {
namespace fw {
namespace hash {

namespace {

// FIPS 180-4 §4.1.2 辅助函数（均操作 std::uint32_t，依赖无符号模 2^32 语义）
inline std::uint32_t rotr(std::uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

inline std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline std::uint32_t bigSigma0(std::uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline std::uint32_t bigSigma1(std::uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline std::uint32_t smallSigma0(std::uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline std::uint32_t smallSigma1(std::uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

// 大端读取 32 位（显式移位组合，避免未对齐指针强转与主机字节序差异）
inline std::uint32_t loadBe32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8)  |
           (static_cast<std::uint32_t>(p[3]));
}

// 大端写入 32 位
inline void storeBe32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

// 大端写入 64 位（消息 bit 长度）
inline void storeBe64(std::uint8_t* p, std::uint64_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 56);
    p[1] = static_cast<std::uint8_t>(v >> 48);
    p[2] = static_cast<std::uint8_t>(v >> 40);
    p[3] = static_cast<std::uint8_t>(v >> 32);
    p[4] = static_cast<std::uint8_t>(v >> 24);
    p[5] = static_cast<std::uint8_t>(v >> 16);
    p[6] = static_cast<std::uint8_t>(v >> 8);
    p[7] = static_cast<std::uint8_t>(v);
}

// FIPS 180-4 §4.2.2 轮常量 K0..K63
constexpr std::uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

// Hex 编码固定表（§7：不使用 locale/格式化流/可配置大小写）
constexpr char kHexTable[] = "0123456789abcdef";

} // anonymous namespace

namespace detail {

Sha256Context::Sha256Context() {
    // FIPS 180-4 §5.3.3 初始哈希值
    m_state[0] = 0x6a09e667u;
    m_state[1] = 0xbb67ae85u;
    m_state[2] = 0x3c6ef372u;
    m_state[3] = 0xa54ff53au;
    m_state[4] = 0x510e527fu;
    m_state[5] = 0x9b05688cu;
    m_state[6] = 0x1f83d9abu;
    m_state[7] = 0x5be0cd19u;
    m_bitlen = 0;
    m_buffer_len = 0;
}

void Sha256Context::update(const std::uint8_t* data, std::size_t size) {
    if (size == 0) {
        return;
    }
    m_bitlen += static_cast<std::uint64_t>(size) * 8u;

    // 若 buffer 中有残留，先填满一个 64 字节块再处理
    if (m_buffer_len > 0) {
        std::size_t need = 64 - m_buffer_len;
        std::size_t take = (size < need) ? size : need;
        std::memcpy(m_buffer + m_buffer_len, data, take);
        m_buffer_len += take;
        data += take;
        size -= take;
        if (m_buffer_len == 64) {
            processBlock(m_buffer);
            m_buffer_len = 0;
        }
    }

    // 处理完整块（指针随输入推进，不复制完整输入）
    while (size >= 64) {
        processBlock(data);
        data += 64;
        size -= 64;
    }

    // 剩余不足一块存入 buffer
    if (size > 0) {
        std::memcpy(m_buffer, data, size);
        m_buffer_len = size;
    }
}

std::array<std::uint8_t, 32> Sha256Context::finalize() {
    // FIPS 180-4 §5.1.1 padding：追加单个 1 bit（0x80）
    m_buffer[m_buffer_len++] = 0x80u;

    // 若剩余空间不足 8 字节存放 64 位长度，先补满本块并处理
    if (m_buffer_len > 56) {
        while (m_buffer_len < 64) {
            m_buffer[m_buffer_len++] = 0x00u;
        }
        processBlock(m_buffer);
        m_buffer_len = 0;
    }

    // 补 0 至 56 字节（长度模 512 等于 448）
    while (m_buffer_len < 56) {
        m_buffer[m_buffer_len++] = 0x00u;
    }

    // 追加原消息 bit 长度的 64 位大端表示
    storeBe64(m_buffer + 56, m_bitlen);
    processBlock(m_buffer);

    // 最终 8 个 32 位状态值按大端顺序输出为 32 字节摘要
    std::array<std::uint8_t, 32> digest;
    for (int i = 0; i < 8; ++i) {
        storeBe32(digest.data() + i * 4, m_state[i]);
    }
    return digest;
}

void Sha256Context::processBlock(const std::uint8_t* block) {
    std::uint32_t w[64];

    // FIPS 180-4 §6.2.2 消息调度
    for (int t = 0; t < 16; ++t) {
        w[t] = loadBe32(block + t * 4);
    }
    for (int t = 16; t < 64; ++t) {
        w[t] = smallSigma1(w[t - 2]) + w[t - 7] +
               smallSigma0(w[t - 15]) + w[t - 16];
    }

    std::uint32_t a = m_state[0];
    std::uint32_t b = m_state[1];
    std::uint32_t c = m_state[2];
    std::uint32_t d = m_state[3];
    std::uint32_t e = m_state[4];
    std::uint32_t f = m_state[5];
    std::uint32_t g = m_state[6];
    std::uint32_t h = m_state[7];

    // FIPS 180-4 §6.2.2 压缩
    for (int t = 0; t < 64; ++t) {
        std::uint32_t t1 = h + bigSigma1(e) + ch(e, f, g) + kSha256K[t] + w[t];
        std::uint32_t t2 = bigSigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
}

} // namespace detail

// ============================================================
// 公共 API 实现
// ============================================================

Sha256Digest sha256(ByteView input) {
    // 参数校验：非零长度但数据指针为空 -> FW-0401（计算前拒绝，不返回部分摘要）
    if (input.size > 0 && input.data == nullptr) {
        throw HashException(HashError::kInvalidInput,
            "non-zero size with null data pointer",
            "size=" + std::to_string(input.size));
    }
    // bit 长度换算溢出检查：input.size * 8 必须可表示为 std::uint64_t
    if (input.size > (std::numeric_limits<std::uint64_t>::max() / 8u)) {
        throw HashException(HashError::kInvalidInput,
            "input length overflows 64-bit bit length",
            "size=" + std::to_string(input.size));
    }

    detail::Sha256Context ctx;
    ctx.update(input.data, input.size);
    return ctx.finalize();
}

std::string sha256_hex(ByteView input) {
    // 原始摘要计算与 Hex 编码分层：sha256_hex(input) == to_lower_hex(sha256(input))
    Sha256Digest digest = sha256(input);
    std::string hex;
    hex.resize(64);
    for (std::size_t i = 0; i < 32; ++i) {
        std::uint8_t byte = digest[i];
        hex[i * 2]     = kHexTable[(byte >> 4) & 0x0Fu];
        hex[i * 2 + 1] = kHexTable[byte & 0x0Fu];
    }
    return hex;
}

} // namespace hash
} // namespace fw
} // namespace tbox
