#include "config/path_resolver.h"
#include <cassert>
#include <iostream>

using namespace hwyz::config;

void test_path_resolver_basic() {
    PathResolver resolver("prov", "/etc/tbox");

    assert(resolver.getCommonPath() == "/etc/tbox/common.yaml");
    assert(resolver.getServicePath() == "/etc/tbox/conf.d/prov.yaml");
    assert(resolver.getLocalPath() == "./prov.yaml");
    assert(resolver.getServiceName() == "prov");
    assert(resolver.getConfigRoot() == "/etc/tbox/");

    std::cout << "test_path_resolver_basic passed" << std::endl;
}

void test_path_resolver_trailing_slash() {
    PathResolver resolver("sec", "/etc/tbox/");

    assert(resolver.getCommonPath() == "/etc/tbox/common.yaml");
    assert(resolver.getServicePath() == "/etc/tbox/conf.d/sec.yaml");
    assert(resolver.getLocalPath() == "./sec.yaml");

    std::cout << "test_path_resolver_trailing_slash passed" << std::endl;
}

int main() {
    test_path_resolver_basic();
    test_path_resolver_trailing_slash();
    return 0;
}
