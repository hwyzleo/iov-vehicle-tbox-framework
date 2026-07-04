#include "store.h"
#include <cassert>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace hwyz::store;

void test_custom_root_path() {
    const std::string customRoot = "/tmp/tbox_custom_root";
    const std::string serviceName = "test_custom";
    
    // 清理可能存在的目录
    std::string storePath = customRoot + "/" + serviceName + "/";
    rmdir(storePath.c_str());
    rmdir(customRoot.c_str());
    
    // 使用自定义根路径打开存储
    Store store = Store::open(serviceName, customRoot);
    assert(store.isReady() == true);
    
    // 验证目录已创建
    struct stat st;
    assert(stat(storePath.c_str(), &st) == 0);
    assert((st.st_mode & S_IFDIR) != 0);
    
    // 保存和加载数据
    store.save<int>("test_key", 123);
    int value = store.load<int>("test_key");
    assert(value == 123);
    
    // 清理
    store.cleanup();
}

void test_default_root_path() {
    // 注意：默认根路径 /var/lib/tbox 在测试环境中可能不存在且无写权限
    // 因此此测试使用临时目录模拟默认行为
    const std::string serviceName = "test_default";
    const std::string tempRoot = "/tmp/tbox_default_test";
    
    // 清理
    std::string storePath = tempRoot + "/" + serviceName + "/";
    rmdir(storePath.c_str());
    rmdir(tempRoot.c_str());
    
    // 使用临时目录作为根路径（模拟默认行为）
    Store store = Store::open(serviceName, tempRoot);
    assert(store.isReady() == true);
    
    // 验证目录创建
    struct stat st;
    assert(stat(storePath.c_str(), &st) == 0);
    assert((st.st_mode & S_IFDIR) != 0);
    
    // 保存和加载数据
    store.save<std::string>("name", "default_test");
    std::string value = store.load<std::string>("name");
    assert(value == "default_test");
    
    // 清理
    store.cleanup();
}

void test_root_path_from_config_simulation() {
    // 模拟从配置中读取 common.store.root
    // 在实际服务中，这会通过 CONFIG_SNAPSHOT->getString("common.store.root", "/var/lib/tbox") 实现
    std::string configRoot = "/tmp/tbox_config_root";
    std::string serviceName = "test_config";
    
    // 清理
    std::string storePath = configRoot + "/" + serviceName + "/";
    rmdir(storePath.c_str());
    rmdir(configRoot.c_str());
    
    // 模拟配置读取
    std::string storeRoot = configRoot;  // 实际中来自配置
    
    // 打开存储
    Store store = Store::open(serviceName, storeRoot);
    assert(store.isReady() == true);
    
    // 验证目录创建
    struct stat st;
    assert(stat(storePath.c_str(), &st) == 0);
    assert((st.st_mode & S_IFDIR) != 0);
    
    // 测试数据持久化
    store.save<double>("pi", 3.14159);
    double pi = store.load<double>("pi");
    assert(std::abs(pi - 3.14159) < 1e-6);
    
    // 清理
    store.cleanup();
}

void test_root_path_with_trailing_slash() {
    const std::string rootWithSlash = "/tmp/tbox_slash_test/";
    const std::string serviceName = "test_slash";
    
    // 清理
    std::string storePath = rootWithSlash + serviceName + "/";
    rmdir(storePath.c_str());
    rmdir(rootWithSlash.c_str());
    
    // 打开存储
    Store store = Store::open(serviceName, rootWithSlash);
    assert(store.isReady() == true);
    
    // 验证路径处理正确
    store.save<bool>("flag", true);
    bool flag = store.load<bool>("flag");
    assert(flag == true);
    
    // 清理
    store.cleanup();
}

void test_root_path_without_trailing_slash() {
    const std::string rootNoSlash = "/tmp/tbox_no_slash_test";
    const std::string serviceName = "test_no_slash";
    
    // 清理
    std::string storePath = rootNoSlash + "/" + serviceName + "/";
    rmdir(storePath.c_str());
    rmdir(rootNoSlash.c_str());
    
    // 打开存储
    Store store = Store::open(serviceName, rootNoSlash);
    assert(store.isReady() == true);
    
    // 验证路径处理正确（PathResolver会自动添加斜杠）
    store.save<int>("count", 42);
    int count = store.load<int>("count");
    assert(count == 42);
    
    // 清理
    store.cleanup();
}

int main() {
    test_custom_root_path();
    test_default_root_path();
    test_root_path_from_config_simulation();
    test_root_path_with_trailing_slash();
    test_root_path_without_trailing_slash();
    
    std::cout << "All configurable path tests passed!" << std::endl;
    return 0;
}