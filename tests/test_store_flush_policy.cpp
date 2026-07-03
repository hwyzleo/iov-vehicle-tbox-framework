#include "store/store_flush_policy.h"
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

void test_get_dirty_keys() {
    FlushPolicy policy;

    // 初始状态没有脏键
    assert(policy.getDirtyKeys().empty());

    // 标记一些脏条目
    policy.markDirty("key1");
    policy.markDirty("key2");
    policy.markDirty("key3");

    auto keys = policy.getDirtyKeys();
    assert(keys.size() == 3);

    // 验证包含所有脏键
    bool hasKey1 = false, hasKey2 = false, hasKey3 = false;
    for (const auto& key : keys) {
        if (key == "key1") hasKey1 = true;
        if (key == "key2") hasKey2 = true;
        if (key == "key3") hasKey3 = true;
    }
    assert(hasKey1 && hasKey2 && hasKey3);

    // 清除一个后，getDirtyKeys应该只返回剩余的脏键
    policy.clearDirty("key2");
    keys = policy.getDirtyKeys();
    assert(keys.size() == 2);

    hasKey1 = false; hasKey2 = false; hasKey3 = false;
    for (const auto& key : keys) {
        if (key == "key1") hasKey1 = true;
        if (key == "key2") hasKey2 = true;
        if (key == "key3") hasKey3 = true;
    }
    assert(hasKey1 && !hasKey2 && hasKey3);
}

void test_reset() {
    FlushPolicy policy;

    // 标记一些脏条目
    policy.markDirty("key1");
    policy.markDirty("key2");
    assert(policy.getDirtyKeys().size() == 2);

    // reset应该清除所有状态
    policy.reset();
    assert(policy.getDirtyKeys().empty());
    assert(policy.isDirty("key1") == false);
    assert(policy.isDirty("key2") == false);

    // reset后可以继续正常使用
    policy.markDirty("key3");
    assert(policy.isDirty("key3") == true);
}

int main() {
    test_dirty_marking();
    test_should_flush();
    test_debounce();
    test_get_dirty_keys();
    test_reset();
    std::cout << "All flush policy tests passed!" << std::endl;
    return 0;
}
