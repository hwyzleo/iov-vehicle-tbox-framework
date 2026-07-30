//
// test_application_shutdown.cpp
//
// TBOX-FW-DSN-CR-007: Application 长驻主循环退出能力单测。
// 覆盖 isShutdownRequested/requestShutdown/waitForShutdown 的
// 初值、跨线程可见性、幂等性与等待延迟。
//

#include "application.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

class TestApp : public hwyz::Application {
public:
    using Application::isShutdownRequested;
    using Application::requestShutdown;
    using Application::waitForShutdown;

    int execute() override { return 0; }
};

void test_initially_not_requested() {
    TestApp app;
    assert(!app.isShutdownRequested());
    std::cout << "  [PASS] test_initially_not_requested" << std::endl;
}

void test_request_sets_flag() {
    TestApp app;
    assert(!app.isShutdownRequested());
    app.requestShutdown();
    assert(app.isShutdownRequested());
    std::cout << "  [PASS] test_request_sets_flag" << std::endl;
}

void test_request_idempotent() {
    TestApp app;
    app.requestShutdown();
    app.requestShutdown();
    app.requestShutdown();
    assert(app.isShutdownRequested());
    std::cout << "  [PASS] test_request_idempotent" << std::endl;
}

void test_cross_thread_visibility() {
    TestApp app;
    assert(!app.isShutdownRequested());
    std::thread t([&app]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        app.requestShutdown();
    });
    // 自旋等待，acquire/release 保证可见性
    while (!app.isShutdownRequested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(app.isShutdownRequested());
    t.join();
    std::cout << "  [PASS] test_cross_thread_visibility" << std::endl;
}

void test_wait_returns_after_request() {
    TestApp app;
    auto start = std::chrono::steady_clock::now();
    std::thread t([&app]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        app.requestShutdown();
    });
    // poll=20ms；应在大约 50~70ms 后返回（非忙等）
    app.waitForShutdown(std::chrono::milliseconds(20));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
    assert(app.isShutdownRequested());
    assert(elapsed >= std::chrono::milliseconds(40));
    assert(elapsed < std::chrono::milliseconds(1000));
    t.join();
    std::cout << "  [PASS] test_wait_returns_after_request (" << elapsed.count() << "ms)"
              << std::endl;
}

void test_wait_default_poll_when_nonpositive() {
    TestApp app;
    std::thread t([&app]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        app.requestShutdown();
    });
    // poll<=0 应使用默认 100ms，不忙等也不立即返回
    app.waitForShutdown(std::chrono::milliseconds(0));
    assert(app.isShutdownRequested());
    t.join();
    std::cout << "  [PASS] test_wait_default_poll_when_nonpositive" << std::endl;
}

int main() {
    std::cout << "Running application shutdown tests..." << std::endl;
    test_initially_not_requested();
    test_request_sets_flag();
    test_request_idempotent();
    test_cross_thread_visibility();
    test_wait_returns_after_request();
    test_wait_default_poll_when_nonpositive();
    std::cout << "All application shutdown tests passed!" << std::endl;
    return 0;
}
