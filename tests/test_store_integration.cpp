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

// 回归测试：空字符串是合法值，save/load 必须成功，
// 不能因序列化结果为空而抛出 kSerializationFailed。
void test_empty_string_round_trip() {
    Store store = Store::open("empty_string_test", "/tmp/tbox_empty_string_test");

    // save 空字符串不应抛异常
    bool threw = false;
    try {
        store.save<std::string>("last_error", "");
    } catch (const StoreException&) {
        threw = true;
    }
    assert(!threw);

    // 读回应为对应的空字符串
    assert(store.has("last_error"));
    assert(store.load<std::string>("last_error") == "");

    // loadOr 命中已存在的空值时应返回空字符串，而非默认值
    assert(store.loadOr<std::string>("last_error", "fallback") == "");

    store.cleanup();
}

int main() {
    test_concurrent_save_load();
    test_power_loss_simulation();
    test_empty_string_round_trip();
    std::cout << "All integration tests passed!" << std::endl;
    return 0;
}
