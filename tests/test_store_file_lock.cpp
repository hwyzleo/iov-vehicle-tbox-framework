#include "store_file_lock.h"
#include "store_path_resolver.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using namespace hwyz::store;

void test_basic_lock_unlock() {
    PathResolver resolver("test_lock", "/tmp/tbox_test_lock");
    resolver.ensureDirectory();

    FileLock lock(resolver);

    bool acquired = lock.acquire("counter");
    assert(acquired == true);

    lock.release("counter");

    std::remove(resolver.getStorePath().c_str());
    std::remove("/tmp/tbox_test_lock");
}

void test_concurrent_access() {
    PathResolver resolver("test_concurrent", "/tmp/tbox_test_concurrent");
    resolver.ensureDirectory();

    FileLock lock(resolver);
    std::atomic<int> counter(0);

    auto worker = [&]() {
        for (int i = 0; i < 100; ++i) {
            lock.acquire("counter");
            counter++;
            lock.release("counter");
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    assert(counter == 400);

    std::remove(resolver.getStorePath().c_str());
    std::remove("/tmp/tbox_test_concurrent");
}

int main() {
    test_basic_lock_unlock();
    test_concurrent_access();
    std::cout << "All file lock tests passed!" << std::endl;
    return 0;
}
