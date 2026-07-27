// tests/test_ipc_integration.cpp
// 回环集成测试：临时 socket 路径，call -> subscribe -> push -> 断线重连 -> 优雅停机
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

using namespace tbox::fw::ipc;

static const char* INTEGRATION_SOCKET = "/tmp/tbox-test-ipc-integration.sock";

// ============================================================
// Full loopback: call, subscribe, push, graceful shutdown
// ============================================================
void test_full_loopback() {
    // Start server
    Server server(INTEGRATION_SOCKET);
    std::atomic<int> captured_fd{-1};

    auto handler = [&captured_fd](uint32_t method_id, std::string_view params, int client_fd) -> std::string {
        if (method_id == 1) {
            // Echo method
            return std::string(params);
        }
        if (method_id == 10) {
            // Subscribe method
            captured_fd.store(client_fd);
            return R"({"subscribed":true})";
        }
        return R"({"error":"unknown"})";
    };

    assert(server.start(handler));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Create client
    Client client(INTEGRATION_SOCKET);

    // Test 1: Basic call
    {
        auto [status, resp] = client.call(1, R"({"test":"call"})");
        assert(status == 0);
        assert(resp == R"({"test":"call"})");
    }

    // Test 2: Subscribe and receive events
    std::atomic<int> event_count{0};
    std::string last_event_payload;

    {
        Subscription sub = client.subscribe(10, 100, [&](uint32_t event_type, std::string_view payload) {
            if (event_type == 100) {
                event_count.fetch_add(1);
                last_event_payload = std::string(payload);
            }
        });

        // Wait for subscription to establish
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        // Add subscription on server side
        int fd = captured_fd.load();
        assert(fd >= 0);
        assert(server.add_subscription(fd, 100));

        // Push events
        assert(server.push_event(100, R"({"event":"integration_test"})"));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        assert(event_count.load() == 1);
        assert(last_event_payload == R"({"event":"integration_test"})");

        // Push another event
        assert(server.push_event(100, R"({"seq":2})"));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        assert(event_count.load() == 2);

        // Cancel subscription
        sub.cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Push after cancel should not increment count
        int before = event_count.load();
        server.push_event(100, R"({"after":"cancel"})");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(event_count.load() == before);
    }

    // Test 3: Call still works after subscription cancelled
    {
        auto [status, resp] = client.call(1, R"({"after":"sub_cancel"})");
        assert(status == 0);
        assert(resp == R"({"after":"sub_cancel"})");
    }

    // Test 4: Graceful shutdown
    client.disconnect();
    server.stop();

    // Socket file should be cleaned up
    assert(access(INTEGRATION_SOCKET, F_OK) != 0);

    std::cout << "  [PASS] test_full_loopback" << std::endl;
}

// ============================================================
// Reconnect after server restart
// ============================================================
void test_reconnect_after_restart() {
    const char* sock = "/tmp/tbox-test-ipc-reconnect.sock";

    // First server instance
    {
        Server server(sock);
        auto handler = [](uint32_t, std::string_view, int) -> std::string {
            return R"({"instance":1})";
        };
        assert(server.start(handler));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        Client client(sock);
        auto [status1, resp1] = client.call(1, "{}");
        assert(status1 == 0);
        assert(resp1 == R"({"instance":1})");

        server.stop();
    }

    // Second server instance on same path
    {
        Server server(sock);
        auto handler = [](uint32_t, std::string_view, int) -> std::string {
            return R"({"instance":2})";
        };
        assert(server.start(handler));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        Client client(sock);
        // Should connect to new server (old connection was closed)
        auto [status2, resp2] = client.call(1, "{}");
        assert(status2 == 0);
        assert(resp2 == R"({"instance":2})");

        server.stop();
    }

    unlink(sock);
    std::cout << "  [PASS] test_reconnect_after_restart" << std::endl;
}

// ============================================================
// Multiple clients with mixed call and subscribe
// ============================================================
void test_multiple_clients_mixed() {
    const char* sock = "/tmp/tbox-test-ipc-multi.sock";
    Server server(sock);
    std::atomic<int> subscribe_fd{-1};

    auto handler = [&subscribe_fd](uint32_t method_id, std::string_view, int client_fd) -> std::string {
        if (method_id == 10) {
            subscribe_fd.store(client_fd);
            return "{}";
        }
        return R"({"ok":true})";
    };

    assert(server.start(handler));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Client 1: caller
    Client caller(sock);

    // Client 2: subscriber
    Client subscriber(sock);
    std::atomic<int> events{0};

    Subscription sub = subscriber.subscribe(10, 200, [&](uint32_t, std::string_view) {
        events.fetch_add(1);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    int fd = subscribe_fd.load();
    assert(fd >= 0);
    server.add_subscription(fd, 200);

    // Caller makes a call while subscriber receives events
    auto [status, resp] = caller.call(1, "{}");
    assert(status == 0);

    server.push_event(200, R"({"multi":true})");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    assert(events.load() == 1);

    // Caller can still call after event push
    auto [status2, resp2] = caller.call(1, "{}");
    assert(status2 == 0);

    sub.cancel();
    server.stop();
    unlink(sock);

    std::cout << "  [PASS] test_multiple_clients_mixed" << std::endl;
}

// ============================================================
// Main
// ============================================================
int main() {
    std::cout << "=== IPC Integration Tests ===" << std::endl;

    unlink(INTEGRATION_SOCKET);

    test_full_loopback();
    test_reconnect_after_restart();
    test_multiple_clients_mixed();

    unlink(INTEGRATION_SOCKET);

    std::cout << "=== All Integration tests passed ===" << std::endl;
    return 0;
}
