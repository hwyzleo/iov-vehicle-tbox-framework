#include "store_path_resolver.h"
#include <cassert>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

using namespace hwyz::store;

void test_path_generation() {
    PathResolver resolver("prov", "/var/lib/tbox");

    assert(resolver.getStorePath() == "/var/lib/tbox/prov/");
    assert(resolver.getKeyPath("counter") == "/var/lib/tbox/prov/counter.dat");
    assert(resolver.getKeyPath("session.id") == "/var/lib/tbox/prov/session.id.dat");
    assert(resolver.getTempPath("counter") == "/var/lib/tbox/prov/counter.dat.tmp");
}

void test_directory_creation() {
    PathResolver resolver("test_svc", "/tmp/tbox_test_store");

    bool created = resolver.ensureDirectory();
    assert(created == true);

    struct stat st;
    assert(stat(resolver.getStorePath().c_str(), &st) == 0);
    assert((st.st_mode & S_IFDIR) != 0);

    rmdir(resolver.getStorePath().c_str());
    rmdir("/tmp/tbox_test_store");
}

int main() {
    test_path_generation();
    test_directory_creation();
    std::cout << "All path resolver tests passed!" << std::endl;
    return 0;
}
