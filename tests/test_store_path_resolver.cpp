#include "store/store_path_resolver.h"
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

void test_empty_service_name() {
    bool threw = false;
    try { PathResolver("", "/var/lib/tbox"); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
}

void test_directory_exists_false() {
    PathResolver resolver("nonexistent_svc", "/tmp/tbox_test_missing");
    assert(resolver.directoryExists() == false);
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
    test_empty_service_name();
    test_directory_exists_false();
    test_directory_creation();
    std::cout << "All path resolver tests passed!" << std::endl;
    return 0;
}
