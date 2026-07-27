// tests/test_ipc_compatibility.cpp
// 兼容测试：验证 framework-ipc wire 格式与 prov/sec/tsp 现有实现逐字节一致
#include "ipc.h"
#include "ipc_types.h"
#include "ipc/ipc_protocol.h"

#include <cassert>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <chrono>
#include <vector>

using namespace tbox::fw::ipc;

// ============================================================
// Simulate existing service wire format (matching tsp/sec/prov)
// Uses __attribute__((packed)) and memcpy, same as existing code
// ============================================================
namespace legacy {

struct RequestHeader {
    uint32_t method_id;
    uint32_t params_length;
} __attribute__((packed));

struct ResponseHeader {
    int32_t status_code;
    uint32_t data_length;
} __attribute__((packed));

struct EventHeader {
    uint32_t event_type;
    uint32_t payload_length;
} __attribute__((packed));

// Serialize request exactly as existing services do
std::vector<uint8_t> serialize_request(uint32_t method, const std::string& params_json) {
    RequestHeader header;
    header.method_id = method;
    header.params_length = static_cast<uint32_t>(params_json.size());

    std::vector<uint8_t> result(sizeof(header) + params_json.size());
    memcpy(result.data(), &header, sizeof(header));
    memcpy(result.data() + sizeof(header), params_json.data(), params_json.size());
    return result;
}

// Serialize response exactly as existing services do
std::vector<uint8_t> serialize_response(int32_t status_code, const std::string& response_json) {
    ResponseHeader header;
    header.status_code = status_code;
    header.data_length = static_cast<uint32_t>(response_json.size());

    std::vector<uint8_t> result(sizeof(header) + response_json.size());
    memcpy(result.data(), &header, sizeof(header));
    memcpy(result.data() + sizeof(header), response_json.data(), response_json.size());
    return result;
}

// Serialize event exactly as existing services do
std::vector<uint8_t> serialize_event(uint32_t event_type, const std::string& payload_json) {
    EventHeader header;
    header.event_type = event_type;
    header.payload_length = static_cast<uint32_t>(payload_json.size());

    std::vector<uint8_t> result(sizeof(header) + payload_json.size());
    memcpy(result.data(), &header, sizeof(header));
    memcpy(result.data() + sizeof(header), payload_json.data(), payload_json.size());
    return result;
}

// Base64 encode (same algorithm as existing services)
static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string base64_encode(const std::string& data) {
    std::string result;
    int i = 0;
    int j = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];

    for (auto byte : data) {
        char_array_3[i++] = static_cast<uint8_t>(byte);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (i = 0; i < 4; i++) {
                result += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;
        for (j = 0; j < i + 1; j++) {
            result += base64_chars[char_array_4[j]];
        }
        while (i++ < 3) {
            result += '=';
        }
    }

    return result;
}

} // namespace legacy

// ============================================================
// 1. Golden bytes: Request
// ============================================================
void test_golden_request_bytes() {
    // Test with various method_ids and payloads (matching tsp MethodId values)
    struct TestCase {
        uint32_t method_id;
        std::string params;
    };

    TestCase cases[] = {
        {1, R"({"online":true})"},           // GET_NET_STATUS
        {2, R"({"inventory":["pkg1"]})"},    // REPORT_SOFTWARE_INVENTORY
        {3, ""},                               // SUBSCRIBE_NET_STATUS (empty params)
        {10, R"({"did":"F190"})"},           // SEC GET_DEVICE_INFO
    };

    for (auto& tc : cases) {
        // Legacy encoding
        auto legacy_bytes = legacy::serialize_request(tc.method_id, tc.params);

        // Framework encoding
        std::string fw_frame = Protocol::encodeRequest(tc.method_id, tc.params);

        // Compare byte-by-byte
        assert(legacy_bytes.size() == fw_frame.size());
        assert(memcmp(legacy_bytes.data(), fw_frame.data(), legacy_bytes.size()) == 0);
    }

    std::cout << "  [PASS] test_golden_request_bytes" << std::endl;
}

// ============================================================
// 2. Golden bytes: Response
// ============================================================
void test_golden_response_bytes() {
    struct TestCase {
        int32_t status_code;
        std::string response;
    };

    TestCase cases[] = {
        {0, R"({"online":true})"},
        {0, ""},
        {306, R"({"error":"method_not_found"})"},
        {-1, R"({"error":"internal"})"},
    };

    for (auto& tc : cases) {
        auto legacy_bytes = legacy::serialize_response(tc.status_code, tc.response);
        std::string fw_frame = Protocol::encodeResponse(tc.status_code, tc.response);

        assert(legacy_bytes.size() == fw_frame.size());
        assert(memcmp(legacy_bytes.data(), fw_frame.data(), legacy_bytes.size()) == 0);
    }

    std::cout << "  [PASS] test_golden_response_bytes" << std::endl;
}

// ============================================================
// 3. Golden bytes: Event
// ============================================================
void test_golden_event_bytes() {
    struct TestCase {
        uint32_t event_type;
        std::string payload;
    };

    TestCase cases[] = {
        {100, R"({"online":true})"},        // NET_STATUS_CHANGED
        {101, R"({"cmd":"reboot"})"},       // REMOTE_COMMAND
        {102, R"({"fota":"update"})"},      // FOTA_COMMAND
    };

    for (auto& tc : cases) {
        auto legacy_bytes = legacy::serialize_event(tc.event_type, tc.payload);
        std::string fw_frame = Protocol::encodeEvent(tc.event_type, tc.payload);

        assert(legacy_bytes.size() == fw_frame.size());
        assert(memcmp(legacy_bytes.data(), fw_frame.data(), legacy_bytes.size()) == 0);
    }

    std::cout << "  [PASS] test_golden_event_bytes" << std::endl;
}

// ============================================================
// 4. Base64 compatibility
// ============================================================
void test_base64_compatibility() {
    std::string test_inputs[] = {
        "",
        "f",
        "fo",
        "foo",
        "foobar",
        "Hello World!",
        "Binary\x00\x01\x02\x03\xFF data",
    };

    for (auto& input : test_inputs) {
        std::string legacy_result = legacy::base64_encode(input);
        std::string fw_result = Protocol::base64Encode(input);
        assert(legacy_result == fw_result);
    }

    std::cout << "  [PASS] test_base64_compatibility" << std::endl;
}

// ============================================================
// 5. Interop: legacy client -> framework server
// ============================================================
void test_legacy_client_framework_server() {
    const char* sock = "/tmp/tbox-test-ipc-compat-1.sock";
    Server server(sock);

    auto handler = [](uint32_t method_id, std::string_view params, int) -> std::string {
        return R"({"echo":)" + std::string(params) + R"(})";
    };

    assert(server.start(handler));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Legacy client (raw socket with legacy serialization)
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(fd >= 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock, sizeof(addr.sun_path) - 1);
    assert(connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);

    // Send request using legacy format
    std::string params = R"({"test":"compat"})";
    auto legacy_frame = legacy::serialize_request(1, params);
    assert(Protocol::writeAll(fd, legacy_frame.data(), legacy_frame.size()));

    // Read response using framework Protocol (should parse legacy format)
    int32_t status_code = -1;
    std::string response_json;
    assert(Protocol::readResponse(fd, status_code, response_json, 10485760));
    assert(status_code == 0);
    assert(response_json == R"({"echo":)" + params + R"(})");

    close(fd);
    server.stop();
    unlink(sock);

    std::cout << "  [PASS] test_legacy_client_framework_server" << std::endl;
}

// ============================================================
// 6. Interop: framework client -> legacy server
// ============================================================
void test_framework_client_legacy_server() {
    const char* sock = "/tmp/tbox-test-ipc-compat-2.sock";

    // Create a "legacy server" using raw socket
    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(listen_fd >= 0);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock, sizeof(addr.sun_path) - 1);
    unlink(sock);
    assert(bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
    assert(listen(listen_fd, 5) == 0);

    // Start accept thread
    std::thread accept_thread([&]() {
        int client_fd = accept(listen_fd, nullptr, nullptr);
        assert(client_fd >= 0);

        // Read request using legacy format
        uint32_t method_id = 0;
        std::string params;
        assert(Protocol::readRequest(client_fd, method_id, params, 10485760));
        assert(method_id == 1);
        assert(params == R"({"compat":true})");

        // Send response using legacy serialization
        auto legacy_resp = legacy::serialize_response(0, R"({"legacy":"ok"})");
        assert(Protocol::writeAll(client_fd, legacy_resp.data(), legacy_resp.size()));

        close(client_fd);
    });

    // Framework client
    Client client(sock);
    auto [status, resp] = client.call(1, R"({"compat":true})");
    assert(status == 0);
    assert(resp == R"({"legacy":"ok"})");

    accept_thread.join();
    close(listen_fd);
    unlink(sock);

    std::cout << "  [PASS] test_framework_client_legacy_server" << std::endl;
}

// ============================================================
// 7. Packed struct size compatibility
// ============================================================
void test_packed_struct_compatibility() {
    // Verify framework headers have same size as legacy headers
    assert(sizeof(RequestHeader) == sizeof(legacy::RequestHeader));
    assert(sizeof(ResponseHeader) == sizeof(legacy::ResponseHeader));
    assert(sizeof(EventHeader) == sizeof(legacy::EventHeader));

    // All should be 8 bytes
    assert(sizeof(RequestHeader) == 8);
    assert(sizeof(ResponseHeader) == 8);
    assert(sizeof(EventHeader) == 8);

    // Verify field offsets match
    RequestHeader rh;
    assert(reinterpret_cast<char*>(&rh.method_id) == reinterpret_cast<char*>(&rh));
    assert(reinterpret_cast<char*>(&rh.params_length) == reinterpret_cast<char*>(&rh) + 4);

    ResponseHeader rsh;
    assert(reinterpret_cast<char*>(&rsh.status_code) == reinterpret_cast<char*>(&rsh));
    assert(reinterpret_cast<char*>(&rsh.data_length) == reinterpret_cast<char*>(&rsh) + 4);

    EventHeader eh;
    assert(reinterpret_cast<char*>(&eh.event_type) == reinterpret_cast<char*>(&eh));
    assert(reinterpret_cast<char*>(&eh.payload_length) == reinterpret_cast<char*>(&eh) + 4);

    std::cout << "  [PASS] test_packed_struct_compatibility" << std::endl;
}

// ============================================================
// Main
// ============================================================
int main() {
    std::cout << "=== IPC Compatibility Tests ===" << std::endl;

    test_packed_struct_compatibility();
    test_golden_request_bytes();
    test_golden_response_bytes();
    test_golden_event_bytes();
    test_base64_compatibility();
    test_legacy_client_framework_server();
    test_framework_client_legacy_server();

    std::cout << "=== All Compatibility tests passed ===" << std::endl;
    return 0;
}
