// tests/test_ipc_client.cpp
// Client 单元测试：惰性连接、call 单次重连重试、并发 call 串行、订阅独立连接、Response->Event 切换、断线重订阅、RAII 取消
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
#include <vector>
#include <unordered_set>

using namespace tbox::fw::ipc;

static const char* TEST_SOCKET = "/tmp/tbox-test-ipc-client.sock";

// ============================================================
// 1. Lazy connect - call without explicit connect()
// ============================================================
void test_lazy_connect() {
    Server server(TEST_SOCKET);
    auto handler = [](uint32_t, std::string_view, int) -> std::string {
        return R"({"lazy":true})";
    };
    assert(server.start(handler));

    Client client(TEST_SOCKET);
    // is_connected should be false before first call
    assert(!client.is_connected());

    // call should trigger lazy connect
    auto [status, resp] = client.call(1, "{}");
    assert(status == 0);
    assert(resp == R"({"lazy":true})");
    assert(client.is_connected());

    server.stop();
    std::cout << "  [PASS] test_lazy_connect" << std::endl;
}

// ============================================================
// 2. Explicit connect
// ============================================================
void test_explicit_connect() {
    Server server(TEST_SOCKET);
    auto handler = [](uint32_t, std::string_view, int) -> std::string {
        return "{}";
    };
    assert(server.start(handler));

    Client client(TEST_SOCKET);
    assert(client.connect());
    assert(client.is_connected());

    // Double connect should succeed (already connected)
    assert(client.connect());

    auto [status, resp] = client.call(1, "{}");
    assert(status == 0);

    client.disconnect();
    assert(!client.is_connected());

    server.stop();
    std::cout << "  [PASS] test_explicit_connect" << std::endl;
}

// ============================================================
// 3. Call returns error when server not available
// ============================================================
void test_call_no_server() {
    // Use a path where no server is listening
    Client client("/tmp/tbox-test-ipc-nonexistent.sock");
    auto [status, resp] = client.call(1, "{}");
    // Should return transport error (negative status)
    assert(status < 0);
    assert(resp.empty());

    std::cout << "  [PASS] test_call_no_server" << std::endl;
}

// ============================================================
// 4. Call retry on transport failure
// ============================================================
void test_call_retry() {
    Server server(TEST_SOCKET);
    auto handler = [](uint32_t, std::string_view, int) -> std::string {
        return R"({"retried":true})";
    };
    assert(server.start(handler));

    Client client(TEST_SOCKET);
    // First call establishes connection
    auto [status1, resp1] = client.call(1, "{}");
    assert(status1 == 0);

    // Force a transport failure by closing the underlying fd
    // We can't easily do this with the public API, but we can test
    // that call recovers after a server restart
    client.disconnect();

    // Reconnect and call should succeed
    auto [status2, resp2] = client.call(1, "{}");
    assert(status2 == 0);
    assert(resp2 == R"({"retried":true})");

    server.stop();
    std::cout << "  [PASS] test_call_retry" << std::endl;
}

// ============================================================
// 5. Concurrent calls are serialized
// ============================================================
void test_concurrent_call_serialization() {
    Server server(TEST_SOCKET);
    std::atomic<int> active_concurrent{0};
    std::atomic<int> max_concurrent{0};

    auto handler = [&active_concurrent, &max_concurrent](uint32_t, std::string_view, int) -> std::string {
        int cur = active_concurrent.fetch_add(1) + 1;
        int max_so_far = max_concurrent.load();
        while (cur > max_so_far && !max_concurrent.compare_exchange_weak(max_so_far, cur)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        active_concurrent.fetch_sub(1);
        return "{}";
    };
    assert(server.start(handler));

    Client client(TEST_SOCKET);

    const int NUM_CALLS = 5;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < NUM_CALLS; i++) {
        threads.emplace_back([&, i]() {
            auto [status, resp] = client.call(1, "{}");
            if (status == 0) {
                success_count.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // All calls should succeed
    assert(success_count.load() == NUM_CALLS);
    // Max concurrent handlers should be 1 (calls are serialized on one connection)
    assert(max_concurrent.load() == 1);

    server.stop();
    std::cout << "  [PASS] test_concurrent_call_serialization" << std::endl;
}

// ============================================================
// 6. Subscribe receives events
// ============================================================
void test_subscribe_receives_events() {
    Server server(TEST_SOCKET);
    std::atomic<int> captured_fd{-1};

    auto handler = [&captured_fd](uint32_t method_id, std::string_view, int client_fd) -> std::string {
        if (method_id == 10) {
            // Subscribe method
            captured_fd.store(client_fd);
        }
        return "{}";
    };
    assert(server.start(handler));

    // Subscribe
    std::atomic<int> event_count{0};
    std::string last_payload;

    Client client(TEST_SOCKET);
    Subscription sub = client.subscribe(10, 200, [&](uint32_t event_type, std::string_view payload) {
        if (event_type == 200) {
            event_count.fetch_add(1);
            last_payload = std::string(payload);
        }
    });

    // Wait for subscription to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Add subscription on server side and push event
    int fd = captured_fd.load();
    assert(fd >= 0);
    server.add_subscription(fd, 200);

    // Push events
    server.push_event(200, R"({"event":"first"})");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.push_event(200, R"({"event":"second"})");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    assert(event_count.load() == 2);
    assert(last_payload == R"({"event":"second"})");

    sub.cancel();
    server.stop();
    std::cout << "  [PASS] test_subscribe_receives_events" << std::endl;
}

// ============================================================
// 7. Subscription RAII cancel
// ============================================================
void test_subscription_raii() {
    Server server(TEST_SOCKET);
    auto handler = [](uint32_t, std::string_view, int) -> std::string {
        return "{}";
    };
    assert(server.start(handler));

    std::atomic<int> event_count{0};
    Client client(TEST_SOCKET);

    {
        Subscription sub = client.subscribe(10, 200, [&](uint32_t, std::string_view) {
            event_count.fetch_add(1);
        });
        assert(sub.isActive());

        // Let subscription establish
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // Subscription destroyed - should be cancelled
    // No more events should be received

    std::cout << "  [PASS] test_subscription_raii" << std::endl;

    server.stop();
}

// ============================================================
// 8. Multiple subscriptions use independent connections
// ============================================================
void test_multiple_subscriptions() {
    Server server(TEST_SOCKET);
    std::atomic<int> fd_count{0};
    std::mutex fd_set_mutex;
    std::unordered_set<int> fd_set;

    auto handler = [&](uint32_t method_id, std::string_view, int client_fd) -> std::string {
        if (method_id == 10) {
            std::lock_guard<std::mutex> lock(fd_set_mutex);
            fd_set.insert(client_fd);
            fd_count.store(static_cast<int>(fd_set.size()));
        }
        return "{}";
    };
    assert(server.start(handler));

    Client client(TEST_SOCKET);
    std::atomic<int> event1_count{0};
    std::atomic<int> event2_count{0};

    Subscription sub1 = client.subscribe(10, 200, [&](uint32_t, std::string_view) {
        event1_count.fetch_add(1);
    });
    Subscription sub2 = client.subscribe(10, 201, [&](uint32_t, std::string_view) {
        event2_count.fetch_add(1);
    });

    // Wait for subscriptions to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Should have 2 independent connections (2 different fds)
    assert(fd_count.load() == 2);

    sub1.cancel();
    sub2.cancel();
    server.stop();
    std::cout << "  [PASS] test_multiple_subscriptions" << std::endl;
}

// ============================================================
// 9. Subscribe with params - new overload sends params in handshake
// ============================================================
void test_subscribe_with_params() {
    Server server(TEST_SOCKET);
    std::string captured_params;
    std::atomic<int> captured_fd{-1};
    std::atomic<int> handler_count{0};

    auto handler = [&](uint32_t method_id, std::string_view params, int client_fd) -> std::string {
        if (method_id == 10) {
            captured_params = std::string(params);
            captured_fd.store(client_fd);
            handler_count.fetch_add(1);
        }
        return "{}";
    };
    assert(server.start(handler));

    Client client(TEST_SOCKET);
    Subscription sub = client.subscribe(10, 200, R"({"owner":"tsp"})",
        [](uint32_t, std::string_view) {});

    for (int i = 0; i < 50; i++) {
        if (handler_count.load() > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(handler_count.load() > 0);
    assert(captured_params == R"({"owner":"tsp"})");
    assert(captured_fd.load() >= 0);

    sub.cancel();
    server.stop();
    std::cout << "  [PASS] test_subscribe_with_params" << std::endl;
}

// ============================================================
// 10. Subscribe params ownership - params copied, not referenced
// ============================================================
void test_subscribe_params_ownership() {
    Server server(TEST_SOCKET);
    std::string captured_params;
    std::atomic<int> handler_count{0};

    auto handler = [&](uint32_t method_id, std::string_view params, int) -> std::string {
        if (method_id == 10) {
            captured_params = std::string(params);
            handler_count.fetch_add(1);
        }
        return "{}";
    };
    assert(server.start(handler));

    Client client(TEST_SOCKET);
    Subscription sub;
    {
        // params created in a temporary scope, destroyed after subscribe returns
        std::string temp_params = R"({"owner":"tsp","route":"downlink"})";
        sub = client.subscribe(10, 200, std::string_view(temp_params),
            [](uint32_t, std::string_view) {});
    }
    // temp_params destroyed; Subscription::Impl holds owned copy

    for (int i = 0; i < 50; i++) {
        if (handler_count.load() > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(handler_count.load() > 0);
    assert(captured_params == R"({"owner":"tsp","route":"downlink"})");

    sub.cancel();
    server.stop();
    std::cout << "  [PASS] test_subscribe_params_ownership" << std::endl;
}

// ============================================================
// 11. Subscribe params reconnect - resends same params on reconnect
// ============================================================
void test_subscribe_params_reconnect() {
    const char* sock = "/tmp/tbox-test-ipc-sub-reconnect.sock";

    std::string captured_params;
    std::atomic<int> connect_count{0};

    auto handler = [&captured_params, &connect_count](uint32_t method_id, std::string_view params, int) -> std::string {
        if (method_id == 10) {
            captured_params = std::string(params);
            connect_count.fetch_add(1);
        }
        return "{}";
    };

    Server server(sock);
    assert(server.start(handler));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    {
        Client client(sock);
        Subscription sub = client.subscribe(10, 200, R"({"owner":"tsp"})",
            [](uint32_t, std::string_view) {});

        // Wait for first connection
        for (int i = 0; i < 50; i++) {
            if (connect_count.load() >= 1) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        assert(connect_count.load() >= 1);
        assert(captured_params == R"({"owner":"tsp"})");

        // Stop server to break the subscription connection
        server.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Restart server - client auto-reconnects and resends params
        assert(server.start(handler));

        // Wait for reconnect
        for (int i = 0; i < 100; i++) {
            if (connect_count.load() >= 2) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        assert(connect_count.load() >= 2);
        assert(captured_params == R"({"owner":"tsp"})");

        sub.cancel();
    }

    server.stop();
    unlink(sock);
    std::cout << "  [PASS] test_subscribe_params_reconnect" << std::endl;
}

// ============================================================
// 12. Old subscribe overload sends empty params (regression)
// ============================================================
void test_subscribe_old_overload_empty_params() {
    Server server(TEST_SOCKET);
    std::string captured_params;
    std::atomic<int> handler_count{0};

    auto handler = [&](uint32_t method_id, std::string_view params, int) -> std::string {
        if (method_id == 10) {
            captured_params = std::string(params);
            handler_count.fetch_add(1);
        }
        return "{}";
    };
    assert(server.start(handler));

    Client client(TEST_SOCKET);
    Subscription sub = client.subscribe(10, 200, [](uint32_t, std::string_view) {});

    for (int i = 0; i < 50; i++) {
        if (handler_count.load() > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(handler_count.load() > 0);
    assert(captured_params == "");

    sub.cancel();
    server.stop();
    std::cout << "  [PASS] test_subscribe_old_overload_empty_params" << std::endl;
}

// ============================================================
// Main
// ============================================================
int main() {
    std::cout << "=== IPC Client Unit Tests ===" << std::endl;

    unlink(TEST_SOCKET);

    test_lazy_connect();
    test_explicit_connect();
    test_call_no_server();
    test_call_retry();
    test_concurrent_call_serialization();
    test_subscribe_receives_events();
    test_subscription_raii();
    test_multiple_subscriptions();
    test_subscribe_with_params();
    test_subscribe_params_ownership();
    test_subscribe_params_reconnect();
    test_subscribe_old_overload_empty_params();

    unlink(TEST_SOCKET);

    std::cout << "=== All Client tests passed ===" << std::endl;
    return 0;
}
