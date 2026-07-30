// tests/test_sha256_cross_platform.cpp
// SHA-256 跨平台 / 差分测试 (CR §10.3)
//
// 以系统可信实现 `openssl dgst -sha256` CLI 作为测试 oracle，对随机输入进行差分
// 测试，验证逐字节一致。openssl CLI 仅作测试 oracle，不构成产品运行时依赖；
// CLI 不可用时优雅跳过（打印 SKIP 并返回 0）。
//
// 注意：route 排序与规范化 golden cases 属 TSP 测试集，不进入本测试 (§10.4)。
#include "hash.h"
#include "hash_types.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

using namespace tbox::fw::hash;

// 检查 openssl CLI 是否可用
static bool opensslAvailable() {
    return std::system("openssl version >/dev/null 2>&1") == 0;
}

// 调用 openssl dgst -sha256，返回小写 hex。
// 通过临时文件传递数据（文件名作为参数，避免 shell 重定向竞态），兼容性最好；
// 返回空串表示失败。
static std::string opensslSha256Hex(const std::uint8_t* data, std::size_t size) {
    char tmpl[] = "/tmp/tbox_hash_diff_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        return "";
    }
    if (size > 0) {
        std::size_t written = 0;
        while (written < size) {
            ssize_t n = write(fd, data + written, size - written);
            if (n <= 0) break;
            written += static_cast<std::size_t>(n);
        }
    }
    close(fd);

    // 以文件名作参数调用（不用 shell 重定向，避免 popen 与 remove 的竞态）
    std::string cmd = "openssl dgst -sha256 \"" + std::string(tmpl) + "\" 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::remove(tmpl);
        return "";
    }
    std::string raw;
    char buffer[256];
    while (std::fgets(buffer, sizeof(buffer), pipe)) {
        raw += buffer;
    }
    pclose(pipe);
    std::remove(tmpl);  // 读取结果后再删除

    // 输出形如 "SHA2-256(/tmp/...)= <hex>\n"，取最后一个非空白 token
    std::string token;
    for (auto it = raw.rbegin(); it != raw.rend(); ++it) {
        if (*it == ' ' || *it == '=' || *it == '\n' || *it == '\r' || *it == '\t') {
            if (!token.empty()) break;
            continue;
        }
        token.push_back(*it);
    }
    std::reverse(token.begin(), token.end());
    return token;
}

// raw digest 与 hex 互验（不依赖外部 oracle）
static void test_raw_hex_consistency() {
    std::mt19937 rng(42);
    for (int i = 0; i < 50; ++i) {
        std::size_t len = static_cast<std::size_t>(rng() % 200);
        std::vector<std::uint8_t> data(len);
        for (std::size_t j = 0; j < len; ++j) {
            data[j] = static_cast<std::uint8_t>(rng() & 0xff);
        }
        ByteView bv{data.data(), data.size()};
        Sha256Digest d = sha256(bv);
        std::string hex = sha256_hex(bv);
        assert(hex.size() == 64);
        for (std::size_t k = 0; k < 32; ++k) {
            char hi = "0123456789abcdef"[(d[k] >> 4) & 0x0f];
            char lo = "0123456789abcdef"[d[k] & 0x0f];
            assert(hex[k * 2] == hi);
            assert(hex[k * 2 + 1] == lo);
        }
    }
    std::cout << "  [PASS] test_raw_hex_consistency" << std::endl;
}

// 差分测试：对 openssl CLI
static void test_differential_openssl() {
    if (!opensslAvailable()) {
        std::cout << "  [SKIP] test_differential_openssl (openssl CLI not available)" << std::endl;
        return;
    }

    std::mt19937 rng(12345);
    // 覆盖 padding 边界长度 + 随机长度
    std::size_t lengths[] = {0, 1, 55, 56, 63, 64, 65, 127, 128, 200, 500, 1000};
    int checks = 0;
    for (std::size_t len : lengths) {
        std::vector<std::uint8_t> data(len);
        for (std::size_t j = 0; j < len; ++j) {
            data[j] = static_cast<std::uint8_t>(rng() & 0xff);
        }
        ByteView bv{data.data(), data.size()};
        std::string ours = sha256_hex(bv);
        std::string oracle = opensslSha256Hex(data.data(), data.size());
        assert(!oracle.empty());
        assert(ours == oracle);
        ++checks;
    }

    // 额外随机长度差分
    for (int i = 0; i < 20; ++i) {
        std::size_t len = static_cast<std::size_t>(rng() % 300);
        std::vector<std::uint8_t> data(len);
        for (std::size_t j = 0; j < len; ++j) {
            data[j] = static_cast<std::uint8_t>(rng() & 0xff);
        }
        ByteView bv{data.data(), data.size()};
        std::string ours = sha256_hex(bv);
        std::string oracle = opensslSha256Hex(data.data(), data.size());
        assert(!oracle.empty());
        assert(ours == oracle);
        ++checks;
    }

    std::cout << "  [PASS] test_differential_openssl (" << checks << " inputs)" << std::endl;
}

// 确定性：同一随机输入多次计算一致
static void test_determinism_random() {
    std::mt19937 rng(7);
    for (int i = 0; i < 10; ++i) {
        std::size_t len = static_cast<std::size_t>(rng() % 100);
        std::vector<std::uint8_t> data(len);
        for (std::size_t j = 0; j < len; ++j) {
            data[j] = static_cast<std::uint8_t>(rng() & 0xff);
        }
        ByteView bv{data.data(), data.size()};
        std::string h1 = sha256_hex(bv);
        std::string h2 = sha256_hex(bv);
        assert(h1 == h2);
    }
    std::cout << "  [PASS] test_determinism_random" << std::endl;
}

int main() {
    std::cout << "Running SHA-256 cross-platform / differential tests..." << std::endl;
    test_raw_hex_consistency();
    test_determinism_random();
    test_differential_openssl();
    std::cout << "All SHA-256 cross-platform tests done!" << std::endl;
    return 0;
}
