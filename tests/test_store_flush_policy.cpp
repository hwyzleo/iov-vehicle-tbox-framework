#include "store_flush_policy.h"
#include <cassert>
#include <iostream>
#include <thread>

using namespace hwyz::store;

void test_dirty_marking() {
    FlushPolicy policy;

    // 标记脏
    policy.markDirty("counter");
    assert(policy.isDirty("counter") == true);

    // 清除脏标记
    policy.clearDirty("counter");
    assert(policy.isDirty("counter") == false);
}

void test_should_flush() {
    FlushPolicyConfig config;
    config.enableDebounce = true;
    config.debounceMs = 100;
    config.maxDirtyCount = 5;

    FlushPolicy policy(config);

    // 初始状态不应该刷新
    assert(policy.shouldFlush() == false);

    // 标记一些脏条目
    for (int i = 0; i < 4; ++i) {
        policy.markDirty("key" + std::to_string(i));
    }

    // 未达到阈值，不应该刷新
    assert(policy.shouldFlush() == false);

    // 达到阈值，应该刷新
    policy.markDirty("key4");
    assert(policy.shouldFlush() == true);
}

void test_debounce() {
    FlushPolicyConfig config;
    config.enableDebounce = true;
    config.debounceMs = 50;

    FlushPolicy policy(config);

    policy.markDirty("counter");

    // 立即检查，不应该刷新（去抖）
    assert(policy.shouldFlush() == false);

    // 等待去抖时间
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // 现在应该刷新
    assert(policy.shouldFlush() == true);
}

int main() {
    test_dirty_marking();
    test_should_flush();
    test_debounce();
    std::cout << "All flush policy tests passed!" << std::endl;
    return 0;
}
