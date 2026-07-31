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
// Subscribe with params + push_event_to full loopback
// ============================================================
void test_subscribe_params_and_push_to_loopback() {
    const char* sock = "/tmp/tbox-test-ipc-params-loopback.sock";
    Server server(sock);
    std::atomic<int> subscriber_fd{-1};
    std::string captured_params;

    auto handler = [&subscriber_fd, &captured_params](uint32_t method_id, std::string_view params, int client_fd) -> std::string {
        if (method_id == 10) {
            subscriber_fd.store(client_fd);
            captured_params = std::string(params);
        }
        return "{}";
    };
    assert(server.start(handler));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    Client client(sock);
    std::atomic<int> event_count{0};
    std::string last_payload;

    Subscription sub = client.subscribe(10, 700, R"({"owner":"tsp"})",
        [&](uint32_t et, std::string_view payload) {
            if (et == 700) {
                event_count.fetch_add(1);
                last_payload = std::string(payload);
            }
        });

    for (int i = 0; i < 50; i++) {
        if (subscriber_fd.load() >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(subscriber_fd.load() >= 0);
    assert(captured_params == R"({"owner":"tsp"})");

    int fd = subscriber_fd.load();
    assert(server.add_subscription(fd, 700));
    assert(server.push_event_to(fd, 700, R"({"routed":"downlink"})"));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(event_count.load() == 1);
    assert(last_payload == R"({"routed":"downlink"})");

    sub.cancel();
    server.stop();
    unlink(sock);
    std::cout << "  [PASS] test_subscribe_params_and_push_to_loopback" << std::endl;
}

// ============================================================
// push_event_to vs broadcast - directed doesn't affect others
// ============================================================
void test_push_event_to_vs_broadcast() {
    const char* sock = "/tmp/tbox-test-ipc-to-vs-broadcast.sock";
    Server server(sock);
    std::atomic<int> fd1{-1};
    std::atomic<int> fd2{-1};
    std::atomic<int> fd_index{0};

    auto handler = [&fd1, &fd2, &fd_index](uint32_t, std::string_view, int client_fd) -> std::string {
        int idx = fd_index.fetch_add(1);
        if (idx == 0) fd1.store(client_fd);
        else if (idx == 1) fd2.store(client_fd);
        return "{}";
    };
    assert(server.start(handler));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock, sizeof(addr.sun_path) - 1);

    auto connect_raw = [&]() -> int {
        int s = socket(AF_UNIX, SOCK_STREAM, 0);
        assert(s >= 0);
        assert(connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
        std::string req = Protocol::encodeRequest(1, "{}");
        assert(Protocol::writeAll(s, req.data(), req.size()));
        int32_t status;
        std::string resp;
        assert(Protocol::readResponse(s, status, resp, 10485760));
        return s;
    };

    int sock1 = connect_raw();
    int sock2 = connect_raw();

    for (int i = 0; i < 50; i++) {
        if (fd1.load() >= 0 && fd2.load() >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    int cfd1 = fd1.load();
    int cfd2 = fd2.load();
    assert(cfd1 >= 0 && cfd2 >= 0);

    assert(server.add_subscription(cfd1, 800));
    assert(server.add_subscription(cfd2, 800));

    // Directed push to fd1 only
    assert(server.push_event_to(cfd1, 800, R"({"dir":"fd1"})"));

    uint32_t et;
    std::string payload;
    assert(Protocol::readEvent(sock1, et, payload, 10485760));
    assert(et == 800 && payload == R"({"dir":"fd1"})");

    // fd2 should NOT receive (short timeout)
    struct timeval short_tv;
    short_tv.tv_sec = 0;
    short_tv.tv_usec = 100000;
    setsockopt(sock2, SOL_SOCKET, SO_RCVTIMEO, &short_tv, sizeof(short_tv));
    char buf[16];
    assert(recv(sock2, buf, sizeof(buf), 0) < 0);

    // Broadcast reaches both
    struct timeval long_tv;
    long_tv.tv_sec = 60;
    long_tv.tv_usec = 0;
    setsockopt(sock2, SOL_SOCKET, SO_RCVTIMEO, &long_tv, sizeof(long_tv));

    assert(server.push_event(800, R"({"bc":true})"));
    assert(Protocol::readEvent(sock1, et, payload, 10485760));
    assert(et == 800 && payload == R"({"bc":true})");
    assert(Protocol::readEvent(sock2, et, payload, 10485760));
    assert(et == 800 && payload == R"({"bc":true})");

    close(sock1);
    close(sock2);
    server.stop();
    unlink(sock);
    std::cout << "  [PASS] test_push_event_to_vs_broadcast" << std::endl;
}

// ============================================================
// Concurrent push_event_to to multiple fds
// ============================================================
void test_concurrent_push_event_to_multi_fd() {
    const char* sock = "/tmp/tbox-test-ipc-concurrent-to.sock";
    Server server(sock);

    const int NUM = 4;
    int fds[NUM];
    std::atomic<int> fd_index{0};

    auto handler = [&fds, &fd_index](uint32_t, std::string_view, int client_fd) -> std::string {
        int idx = fd_index.fetch_add(1);
        if (idx < NUM) {
            fds[idx] = client_fd;
        }
        return "{}";
    };
    assert(server.start(handler));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock, sizeof(addr.sun_path) - 1);

    int socks[NUM];
    for (int i = 0; i < NUM; i++) {
        socks[i] = socket(AF_UNIX, SOCK_STREAM, 0);
        assert(socks[i] >= 0);
        assert(connect(socks[i], reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
        std::string req = Protocol::encodeRequest(1, "{}");
        assert(Protocol::writeAll(socks[i], req.data(), req.size()));
        int32_t status;
        std::string resp;
        assert(Protocol::readResponse(socks[i], status, resp, 10485760));
    }

    for (int i = 0; i < 50; i++) {
        if (fd_index.load() >= NUM) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(fd_index.load() >= NUM);

    int client_fds[NUM];
    for (int i = 0; i < NUM; i++) {
        client_fds[i] = fds[i];
        assert(client_fds[i] >= 0);
        assert(server.add_subscription(client_fds[i], 900));
    }

    // Concurrent push_event_to from multiple threads
    const int EVENTS_PER_FD = 50;
    std::atomic<int> success_count{0};
    std::thread pushers[NUM];

    for (int i = 0; i < NUM; i++) {
        pushers[i] = std::thread([&, i]() {
            for (int j = 0; j < EVENTS_PER_FD; j++) {
                std::string payload = R"({"fd":)" + std::to_string(i) +
                    R"(,"seq":)" + std::to_string(j) + R"(})";
                if (server.push_event_to(client_fds[i], 900, payload)) {
                    success_count.fetch_add(1);
                }
            }
        });
    }
    for (int i = 0; i < NUM; i++) pushers[i].join();
    assert(success_count.load() == NUM * EVENTS_PER_FD);

    // Each client should receive its events in order
    for (int i = 0; i < NUM; i++) {
        for (int j = 0; j < EVENTS_PER_FD; j++) {
            uint32_t et;
            std::string payload;
            assert(Protocol::readEvent(socks[i], et, payload, 10485760));
            assert(et == 900);
            assert(payload == R"({"fd":)" + std::to_string(i) +
                R"(,"seq":)" + std::to_string(j) + R"(})");
        }
    }

    for (int i = 0; i < NUM; i++) close(socks[i]);
    server.stop();
    unlink(sock);
    std::cout << "  [PASS] test_concurrent_push_event_to_multi_fd" << std::endl;
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
    test_subscribe_params_and_push_to_loopback();
    test_push_event_to_vs_broadcast();
    test_concurrent_push_event_to_multi_fd();

    unlink(INTEGRATION_SOCKET);

    std::cout << "=== All Integration tests passed ===" << std::endl;
    return 0;
}
