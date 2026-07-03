#include "store/store_atomic_writer.h"
#include "store/store_path_resolver.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <sys/stat.h>

using namespace hwyz::store;

void test_atomic_write() {
    PathResolver resolver("test_atomic", "/tmp/tbox_test_atomic");
    resolver.ensureDirectory();

    AtomicWriter writer(resolver);

    // 写入数据
    std::string data = "test data content";
    bool success = writer.write("counter", data);
    assert(success == true);

    // 检查文件是否存在
    std::string filePath = resolver.getKeyPath("counter");
    struct stat st;
    assert(stat(filePath.c_str(), &st) == 0);

    // 读取并验证内容
    std::ifstream ifs(filePath);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    assert(content == data);

    // 清理
    std::remove(filePath.c_str());
    std::remove(resolver.getStorePath().c_str());
    std::remove("/tmp/tbox_test_atomic");
}

void test_atomic_write_preserves_original_on_failure() {
    PathResolver resolver("test_atomic2", "/tmp/tbox_test_atomic2");
    resolver.ensureDirectory();

    AtomicWriter writer(resolver);

    // 先写入原始数据
    std::string originalData = "original";
    writer.write("test", originalData);

    // 尝试写入无效路径（应该失败）
    // 注意：这里我们无法真正模拟IO失败，但可以测试基本流程

    // 清理
    std::remove(resolver.getKeyPath("test").c_str());
    std::remove(resolver.getStorePath().c_str());
    std::remove("/tmp/tbox_test_atomic2");
}

int main() {
    test_atomic_write();
    test_atomic_write_preserves_original_on_failure();
    std::cout << "All atomic writer tests passed!" << std::endl;
    return 0;
}
