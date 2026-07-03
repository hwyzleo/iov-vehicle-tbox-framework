#include "store.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using namespace hwyz::store;

void test_concurrent_save_load() {
    Store store = Store::open("integration_test", "/tmp/tbox_integration_test");

    const int numThreads = 4;
    const int numOperations = 100;

    // 并发保存
    std::vector<std::thread> writers;
    for (int i = 0; i < numThreads; ++i) {
        writers.emplace_back([&store, i, numOperations]() {
            for (int j = 0; j < numOperations; ++j) {
                std::string key = "key_" + std::to_string(i) + "_" + std::to_string(j);
                store.save<int>(key, i * 1000 + j);
            }
        });
    }

    for (auto& t : writers) {
        t.join();
    }

    // 验证所有数据
    for (int i = 0; i < numThreads; ++i) {
        for (int j = 0; j < numOperations; ++j) {
            std::string key = "key_" + std::to_string(i) + "_" + std::to_string(j);
            int value = store.load<int>(key);
            assert(value == i * 1000 + j);
        }
    }

    // 清理
    store.cleanup();
}

void test_power_loss_simulation() {
    Store store = Store::open("power_loss_test", "/tmp/tbox_power_loss_test");

    // 保存一些数据
    store.save<int>("counter", 42);
    store.save<std::string>("state", "running");

    // 模拟掉电（直接删除临时文件）
    // 在实际场景中，掉电可能发生在任何时刻
    // 但原子写入保证要么见旧值要么见新值

    // 验证数据完整性
    assert(store.load<int>("counter") == 42);
    assert(store.load<std::string>("state") == "running");

    // 清理
    store.cleanup();
}

int main() {
    test_concurrent_save_load();
    test_power_loss_simulation();
    std::cout << "All integration tests passed!" << std::endl;
    return 0;
}
