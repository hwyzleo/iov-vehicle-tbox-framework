#include "store.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace hwyz::store;

void test_store_open() {
    Store store = Store::open("test_svc", "/tmp/tbox_test_store");
    assert(store.isReady() == true);

    // 清理
    store.cleanup();
}

void test_save_load_int() {
    Store store = Store::open("test_int", "/tmp/tbox_test_store_int");

    // 保存整数
    store.save<int>("counter", 42);

    // 加载整数
    int value = store.load<int>("counter");
    assert(value == 42);

    // 清理
    store.cleanup();
}

void test_save_load_string() {
    Store store = Store::open("test_str", "/tmp/tbox_test_store_str");

    // 保存字符串
    store.save<std::string>("name", "hello world");

    // 加载字符串
    std::string value = store.load<std::string>("name");
    assert(value == "hello world");

    // 清理
    store.cleanup();
}

void test_load_or_default() {
    Store store = Store::open("test_default", "/tmp/tbox_test_store_default");

    // 不存在的key，返回默认值
    int value = store.loadOr<int>("missing", 100);
    assert(value == 100);

    // 清理
    store.cleanup();
}

void test_has_remove() {
    Store store = Store::open("test_has", "/tmp/tbox_test_store_has");

    // 保存
    store.save<int>("counter", 42);

    // 检查存在
    assert(store.has("counter") == true);

    // 删除
    store.remove("counter");
    assert(store.has("counter") == false);

    // 清理
    store.cleanup();
}

void test_exception_on_missing_key() {
    Store store = Store::open("test_exception", "/tmp/tbox_test_store_exception");

    bool exceptionThrown = false;
    try {
        store.load<int>("missing");
    } catch (const StoreException& e) {
        exceptionThrown = true;
        assert(e.getError().code == StoreError::kKeyNotFound);
    }

    assert(exceptionThrown == true);

    // 清理
    store.cleanup();
}

int main() {
    test_store_open();
    test_save_load_int();
    test_save_load_string();
    test_load_or_default();
    test_has_remove();
    test_exception_on_missing_key();
    std::cout << "All store manager tests passed!" << std::endl;
    return 0;
}
