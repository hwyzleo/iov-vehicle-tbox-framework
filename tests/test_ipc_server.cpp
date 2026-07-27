// tests/test_ipc_server.cpp
// Server 单元测试：start/stop 幂等、shutdown_pipe、并发连接、订阅增删、断连清理恰好一次、同 fd 写串行化
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
#include <atomic>
#include <mutex>

using namespace tbox::fw::ipc;

static const char* TEST_SOCKET = "/tmp/tbox-test-ipc-server.sock";

// ============================================================
// 1. start/stop idempotent
// ============================================================
void test_start_stop_idempotent() {
    Server server(TEST_SOCKET);

    // Handler that returns empty response
    auto handler = [](uint32_t, std::string_view, int) -> std::string {
        return R"({"ok":true})";
    };

    assert(server.start(handler));
    // Double start should return true (already running)
    assert(server.start(handler));

    // Stop should be idempotent
    server.stop();
    server.stop();  // No crash

    std::cout << "  [PASS] test_start_stop_idempotent" << std::endl;
}

// ============================================================
// 2. Socket file created and cleaned up
// ============================================================
void test_socket_file_lifecycle() {
    {
        Server server(TEST_SOCKET);
        auto handler = [](uint32_t, std::string_view, int) -> std::string {
            return "{}";
        };
        assert(server.start(handler));

        // Socket file should exist
        assert(access(TEST_SOCKET, F_OK) == 0);
    }
    // After destructor, socket should be unlinked
    assert(access(TEST_SOCKET, F_OK) != 0);

    std::cout << "  [PASS] test_socket_file_lifecycle" << std::endl;
}

// ============================================================
// 3. Concurrent connections - multiple clients can call simultaneously
// ============================================================
void test_concurrent_connections() {
    Server server(TEST_SOCKET);
    std::atomic<int> call_count{0};

    auto handler = [&call_count](uint32_t method_id, std::string_view params, int) -> std::string {
        call_count.fetch_add(1);
        // Simulate some work
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return R"({"handled":true})";
    };

    assert(server.start(handler));

    const int NUM_CLIENTS = 5;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < NUM_CLIENTS; i++) {
        threads.emplace_back([&, i]() {
            Client client(TEST_SOCKET);
            auto [status, resp] = client.call(1, R"({"id":42})");
            if (status == 0 && resp == R"({"handled":true})") {
                success_count.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    assert(call_count.load() == NUM_CLIENTS);
    assert(success_count.load() == NUM_CLIENTS);

    server.stop();
    std::cout << "  [PASS] test_concurrent_connections" << std::endl;
}

// ============================================================
// 4. Request handler dispatch by method_id
// ============================================================
void test_method_dispatch() {
    Server server(TEST_SOCKET);

    auto handler = [](uint32_t method_id, std::string_view params, int) -> std::string {
        if (method_id == 1) {
            return R"({"method":"one"})";
        } else if (method_id == 2) {
            return R"({"method":"two"})";
        }
        return R"({"error":"unknown"})";
    };

    assert(server.start(handler));

    Client client(TEST_SOCKET);

    auto [status1, resp1] = client.call(1, "{}");
    assert(status1 == 0);
    assert(resp1 == R"({"method":"one"})");

    auto [status2, resp2] = client.call(2, "{}");
    assert(status2 == 0);
    assert(resp2 == R"({"method":"two"})");

    auto [status3, resp3] = client.call(999, "{}");
    assert(status3 == 0);
    assert(resp3 == R"({"error":"unknown"})");

    server.stop();
    std::cout << "  [PASS] test_method_dispatch" << std::endl;
}

// ============================================================
// 5. Handler exception maps to FW-0306
// ============================================================
void test_handler_exception() {
    Server server(TEST_SOCKET);

    auto handler = [](uint32_t, std::string_view, int) -> std::string {
        throw std::runtime_error("boom");
    };

    assert(server.start(handler));

    Client client(TEST_SOCKET);
    auto [status, resp] = client.call(1, "{}");
    // status should be 306 (FW-0306, kHandlerFailed)
    assert(status == 306);

    server.stop();
    std::cout << "  [PASS] test_handler_exception" << std::endl;
}

// ============================================================
// 6. Subscription add/remove and push_event
// ============================================================
void test_subscription_and_push() {
    Server server(TEST_SOCKET);
    std::atomic<int> disconnect_count{0};

    auto handler = [](uint32_t, std::string_view, int) -> std::string {
        return "{}";
    };
    auto disconnect_handler = [&disconnect_count](int) {
        disconnect_count.fetch_add(1);
    };

    assert(server.start(handler, disconnect_handler));

    // Connect a raw client to test subscription
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(sock >= 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, TEST_SOCKET, sizeof(addr.sun_path) - 1);
    assert(connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);

    // Give server time to accept
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Add subscription for event_type=100 on this fd
    // We need to find the client_fd. The server accepted our connection.
    // We'll use the handler to get the client_fd.
    // For this test, we'll use a different approach: use the handler to capture the fd.

    // Actually, let's use a simpler approach: the handler returns the client_fd as response
    std::atomic<int> captured_fd{-1};
    server.stop();

    // Restart with handler that captures fd
    auto handler2 = [&captured_fd](uint32_t, std::string_view, int client_fd) -> std::string {
        captured_fd.store(client_fd);
        return "{}";
    };
    assert(server.start(handler2, disconnect_handler));

    // Reconnect
    close(sock);
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(sock >= 0);
    assert(connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);

    // Send a request to capture fd
    std::string req = Protocol::encodeRequest(1, "{}");
    assert(Protocol::writeAll(sock, req.data(), req.size()));

    // Read response
    int32_t status;
    std::string resp;
    assert(Protocol::readResponse(sock, status, resp, 10485760));
    assert(status == 0);

    int client_fd = captured_fd.load();
    assert(client_fd >= 0);

    // Add subscription
    assert(server.add_subscription(client_fd, 100));

    // Push event
    std::string event_payload = R"({"event":"test"})";
    assert(server.push_event(100, event_payload));

    // Read event on client side
    uint32_t event_type;
    std::string payload;
    assert(Protocol::readEvent(sock, event_type, payload, 10485760));
    assert(event_type == 100);
    assert(payload == event_payload);

    // Push to unsubscribed event type should not deliver
    server.push_event(200, R"({"should":"not arrive"})");
    // The client shouldn't receive anything for event_type 200
    // (We can't easily test non-receipt, so just verify no crash)

    // Remove subscription
    assert(server.remove_subscription(client_fd, 100));

    // Push after removal should not deliver (no subscribers)
    assert(!server.push_event(100, R"({"after":"remove"})"));

    close(sock);
    server.stop();

    std::cout << "  [PASS] test_subscription_and_push" << std::endl;
}

// ============================================================
// 7. Disconnect handler called exactly once on natural disconnect
// ============================================================
void test_disconnect_handler_once() {
    Server server(TEST_SOCKET);
    std::atomic<int> disconnect_count{0};

    auto handler = [](uint32_t, std::string_view, int) -> std::string {
        return "{}";
    };
    auto disconnect_handler = [&disconnect_count](int) {
        disconnect_count.fetch_add(1);
    };

    assert(server.start(handler, disconnect_handler));

    // Connect and disconnect
    {
        Client client(TEST_SOCKET);
        auto [status, resp] = client.call(1, "{}");
        assert(status == 0);
    }
    // Client destroyed - should trigger disconnect

    // Wait for disconnect handler
    for (int i = 0; i < 50; i++) {
        if (disconnect_count.load() > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(disconnect_count.load() == 1);

    server.stop();
    std::cout << "  [PASS] test_disconnect_handler_once" << std::endl;
}

// ============================================================
// 8. Same fd write serialization - Response and Event don't interleave
// ============================================================
void test_write_serialization() {
    Server server(TEST_SOCKET);

    std::atomic<int> captured_fd{-1};
    auto handler = [&captured_fd](uint32_t, std::string_view, int client_fd) -> std::string {
        captured_fd.store(client_fd);
        return R"({"response":"data"})";
    };

    assert(server.start(handler));

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(sock >= 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, TEST_SOCKET, sizeof(addr.sun_path) - 1);
    assert(connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);

    // Send request to get fd
    std::string req = Protocol::encodeRequest(1, "{}");
    assert(Protocol::writeAll(sock, req.data(), req.size()));

    int32_t status;
    std::string resp;
    assert(Protocol::readResponse(sock, status, resp, 10485760));
    assert(status == 0);

    int client_fd = captured_fd.load();
    assert(client_fd >= 0);

    // Add subscription
    assert(server.add_subscription(client_fd, 200));

    // Push multiple events rapidly
    for (int i = 0; i < 10; i++) {
        std::string payload = R"({"seq":)" + std::to_string(i) + R"(})";
        server.push_event(200, payload);
    }

    // Read all events - should be complete and in order
    for (int i = 0; i < 10; i++) {
        uint32_t event_type;
        std::string payload;
        assert(Protocol::readEvent(sock, event_type, payload, 10485760));
        assert(event_type == 200);
        assert(payload == R"({"seq":)" + std::to_string(i) + R"(})");
    }

    close(sock);
    server.stop();
    std::cout << "  [PASS] test_write_serialization" << std::endl;
}

// ============================================================
// Main
// ============================================================
int main() {
    std::cout << "=== IPC Server Unit Tests ===" << std::endl;

    // Clean up any leftover socket
    unlink(TEST_SOCKET);

    test_start_stop_idempotent();
    test_socket_file_lifecycle();
    test_concurrent_connections();
    test_method_dispatch();
    test_handler_exception();
    test_subscription_and_push();
    test_disconnect_handler_once();
    test_write_serialization();

    // Final cleanup
    unlink(TEST_SOCKET);

    std::cout << "=== All Server tests passed ===" << std::endl;
    return 0;
}
