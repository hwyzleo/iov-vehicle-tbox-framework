// tests/test_sha256.cpp
// SHA-256 单元测试：标准向量、padding 边界、二进制安全、参数校验、Hex 一致性
//
// 覆盖 TBOX-FW-DSN-CR-006 §10.1~10.2（标准向量、边界与健壮性）。
// route 排序与规范化 golden cases 属 TSP 测试集，不进入本测试。
#include "hash.h"
#include "hash_types.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace tbox::fw::hash;

// ============================================================
// 辅助：ByteView 期望 hex 与实际比对
// ============================================================
static void expectHex(ByteView input, const std::string& expected, const char* label) {
    std::string got = sha256_hex(input);
    assert(got == expected);
    (void)label;
}

// ============================================================
// 1. 标准向量 (CR §10.1)
// ============================================================
void test_standard_empty() {
    // 空输入 -> e3b0c442...
    expectHex(ByteView{nullptr, 0},
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "empty");
    // 同时验证 raw digest
    Sha256Digest d = sha256(ByteView{nullptr, 0});
    assert(d.size() == 32);
    assert(d[0] == 0xe3 && d[1] == 0xb0 && d[2] == 0xc4 && d[3] == 0x42);
    std::cout << "  [PASS] test_standard_empty" << std::endl;
}

void test_standard_abc() {
    std::string s = "abc";
    expectHex(ByteView{reinterpret_cast<const std::uint8_t*>(s.data()), s.size()},
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "abc");
    // string_view 重载一致性
    assert(sha256_hex(s) == sha256_hex(std::string_view(s)));
    std::cout << "  [PASS] test_standard_abc" << std::endl;
}

void test_standard_long_message() {
    // NIST 长消息向量：abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq
    std::string s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    expectHex(ByteView{reinterpret_cast<const std::uint8_t*>(s.data()), s.size()},
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "long");
    std::cout << "  [PASS] test_standard_long_message" << std::endl;
}

void test_standard_million_a() {
    // NIST 百万个 'a' 向量
    std::string million(1000000, 'a');
    expectHex(ByteView{reinterpret_cast<const std::uint8_t*>(million.data()), million.size()},
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "million-a");
    std::cout << "  [PASS] test_standard_million_a" << std::endl;
}

// ============================================================
// 2. padding 边界 (CR §10.2)：0,1,55,56,63,64,65,127,128 字节
// ============================================================
void test_padding_boundaries() {
    struct Case { std::size_t len; const char* hex; };
    Case cases[] = {
        {0,   "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {1,   "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"},
        {55,  "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56,  "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63,  "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64,  "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65,  "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {127, "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a"},
        {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
    };
    for (const auto& c : cases) {
        std::string in(c.len, 'a');
        std::string got = sha256_hex(std::string_view(in));
        assert(got == c.hex);
    }
    std::cout << "  [PASS] test_padding_boundaries" << std::endl;
}

// ============================================================
// 3. 二进制安全 (CR §10.2)
// ============================================================
void test_binary_internal_nul() {
    // 内部 0x00 不截断：含 NUL 的 string_view 按完整长度处理
    std::string s = std::string("ab\0cd", 5);
    assert(s.size() == 5);
    std::string sv_hex = sha256_hex(std::string_view(s));
    // 与显式 ByteView 一致
    ByteView bv{reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
    assert(sha256_hex(bv) == sv_hex);
    // 不等于 "ab" 的摘要（证明 NUL 后字节参与计算）
    assert(sv_hex != sha256_hex(std::string_view("ab")));
    std::cout << "  [PASS] test_binary_internal_nul" << std::endl;
}

void test_binary_all_byte_values() {
    // 全 256 字节值
    std::vector<std::uint8_t> all(256);
    for (int i = 0; i < 256; ++i) {
        all[i] = static_cast<std::uint8_t>(i);
    }
    ByteView bv{all.data(), all.size()};
    Sha256Digest d = sha256(bv);
    assert(d.size() == 32);
    // 与 hex 一致
    std::string hex = sha256_hex(bv);
    assert(hex.size() == 64);
    // 不抛异常即可；具体值由差分测试交叉验证
    std::cout << "  [PASS] test_binary_all_byte_values" << std::endl;
}

void test_binary_non_utf8() {
    // 非 UTF-8 字节序列（含 0xff, 0xfe 等）
    std::uint8_t buf[] = {0xff, 0xfe, 0xfd, 0x80, 0xc0, 0x01, 0x7f, 0x00};
    ByteView bv{buf, sizeof(buf)};
    std::string hex = sha256_hex(bv);
    assert(hex.size() == 64);
    // 与去掉末尾 0x00 的结果不同（0x00 参与计算）
    ByteView bv2{buf, sizeof(buf) - 1};
    assert(sha256_hex(bv2) != hex);
    std::cout << "  [PASS] test_binary_non_utf8" << std::endl;
}

void test_unaligned_start_address() {
    // 不对齐起始地址：从缓冲区偏移 1~7 字节处计算，验证不依赖对齐
    std::string base(100, 'x');
    for (int off = 0; off <= 7; ++off) {
        std::string_view sv(base.data() + off, 50);
        std::string hex = sha256_hex(sv);
        assert(hex.size() == 64);
        // 相同内容、不同偏移应得到相同结果
        std::string same(50, 'x');
        assert(sha256_hex(std::string_view(same)) == hex);
    }
    std::cout << "  [PASS] test_unaligned_start_address" << std::endl;
}

// ============================================================
// 4. 参数边界 (CR §10.2)
// ============================================================
void test_param_empty_nullptr() {
    // ByteView{nullptr, 0} 是合法空输入，成功
    Sha256Digest d = sha256(ByteView{nullptr, 0});
    assert(d.size() == 32);
    assert(d[0] == 0xe3);
    assert(sha256_hex(ByteView{nullptr, 0}) ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::cout << "  [PASS] test_param_empty_nullptr" << std::endl;
}

void test_param_nullptr_nonzero_throws() {
    // ByteView{nullptr, 1} 非法 -> FW-0401
    bool threw = false;
    try {
        sha256(ByteView{nullptr, 1});
    } catch (const HashException& e) {
        threw = true;
        assert(e.getError().code == HashError::kInvalidInput);
    }
    assert(threw);

    // sha256_hex 同样应抛
    threw = false;
    try {
        sha256_hex(ByteView{nullptr, 1});
    } catch (const HashException& e) {
        threw = true;
        assert(e.getError().code == HashError::kInvalidInput);
    }
    assert(threw);
    std::cout << "  [PASS] test_param_nullptr_nonzero_throws" << std::endl;
}

// ============================================================
// 5. Hex 一致性 (CR §10.2)
// ============================================================
static bool isValidHexChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

void test_hex_consistency() {
    std::string inputs[] = {"", "a", "abc", "hello world", std::string(100, 'z')};
    for (const auto& s : inputs) {
        std::string hex = sha256_hex(std::string_view(s));
        // 长度固定 64
        assert(hex.size() == 64);
        // 字符集 [0-9a-f]，小写
        for (char c : hex) {
            assert(isValidHexChar(c));
        }
        // 不含大写
        for (char c : hex) {
            assert(!(c >= 'A' && c <= 'F'));
        }
        // 无前缀/分隔符/空白/换行
        assert(hex.find("0x") == std::string::npos);
        assert(hex.find(' ') == std::string::npos);
        assert(hex.find('\n') == std::string::npos);

        // raw digest 解码后与 hex 一致
        Sha256Digest d = sha256(std::string_view(s));
        for (std::size_t i = 0; i < 32; ++i) {
            std::uint8_t hi = (d[i] >> 4) & 0x0F;
            std::uint8_t lo = d[i] & 0x0F;
            char ehi = "0123456789abcdef"[hi];
            char elo = "0123456789abcdef"[lo];
            assert(hex[i * 2] == ehi);
            assert(hex[i * 2 + 1] == elo);
        }
    }
    std::cout << "  [PASS] test_hex_consistency" << std::endl;
}

// ============================================================
// 6. string_view 重载与 ByteView 一致性
// ============================================================
void test_stringview_overload() {
    // 含内部 NUL 的 string_view 不截断
    std::string s = std::string("foo\0bar", 7);
    std::string_view sv(s);
    assert(sv.size() == 7);

    std::string hex_sv = sha256_hex(sv);
    ByteView bv{reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
    std::string hex_bv = sha256_hex(bv);
    assert(hex_sv == hex_bv);

    // 与 sha256() raw 重载一致
    Sha256Digest d_sv = sha256(sv);
    Sha256Digest d_bv = sha256(bv);
    assert(d_sv == d_bv);
    std::cout << "  [PASS] test_stringview_overload" << std::endl;
}

// ============================================================
// 7. 确定性：相同输入重复计算结果一致
// ============================================================
void test_determinism() {
    std::string s = "TBOX-FW-DSN-CR-006 content_digest";
    std::string h1 = sha256_hex(std::string_view(s));
    std::string h2 = sha256_hex(std::string_view(s));
    assert(h1 == h2);
    std::cout << "  [PASS] test_determinism" << std::endl;
}

int main() {
    std::cout << "Running SHA-256 unit tests..." << std::endl;
    test_standard_empty();
    test_standard_abc();
    test_standard_long_message();
    test_standard_million_a();
    test_padding_boundaries();
    test_binary_internal_nul();
    test_binary_all_byte_values();
    test_binary_non_utf8();
    test_unaligned_start_address();
    test_param_empty_nullptr();
    test_param_nullptr_nonzero_throws();
    test_hex_consistency();
    test_stringview_overload();
    test_determinism();
    std::cout << "All SHA-256 unit tests passed!" << std::endl;
    return 0;
}
