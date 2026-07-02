#include "config.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdio>

using namespace hwyz::config;

void createTempFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
    file.close();
}

void removeTempFile(const std::string& path) {
    std::remove(path.c_str());
}

void test_load_basic() {
    std::string tempDir = "/tmp/test_config";
    std::string commonPath = tempDir + "/common.yaml";
    std::string serviceDir = tempDir + "/conf.d/";
    std::string servicePath = serviceDir + "test.yaml";

    system(("mkdir -p " + serviceDir).c_str());

    createTempFile(commonPath, R"(
log:
  level: info
  format: json
server:
  host: localhost
  port: 8080
)");

    createTempFile(servicePath, R"(
server:
  port: 9090
)");

    ConfigManager& manager = ConfigManager::instance();
    ConfigError error = manager.load("test", tempDir);

    assert(error == ConfigError::kOk);
    assert(manager.isLoaded());

    auto snapshot = manager.getSnapshot();
    assert(snapshot != nullptr);

    assert(snapshot->getString("log.level") == "info");
    assert(snapshot->getString("log.format") == "json");
    assert(snapshot->getString("server.host") == "localhost");
    assert(snapshot->getInt("server.port") == 9090);

    removeTempFile(commonPath);
    removeTempFile(servicePath);
    system(("rmdir " + serviceDir).c_str());
    system(("rmdir " + tempDir).c_str());

    std::cout << "test_load_basic passed" << std::endl;
}

void test_load_missing_common() {
    ConfigManager& manager = ConfigManager::instance();
    ConfigError error = manager.load("test", "/nonexistent/path/");

    assert(error == ConfigError::kFileNotFound);
    assert(!manager.isLoaded());

    std::cout << "test_load_missing_common passed" << std::endl;
}

void test_snapshot_operations() {
    std::string tempDir = "/tmp/test_config_snap";
    std::string commonPath = tempDir + "/common.yaml";

    system(("mkdir -p " + tempDir).c_str());

    createTempFile(commonPath, R"(
log:
  level: info
app:
  name: test-app
  version: 1.0
  debug: true
  tags:
    - tag1
    - tag2
database:
  host: localhost
  port: 5432
)");

    ConfigManager& manager = ConfigManager::instance();
    manager.load("test", tempDir);

    auto snapshot = manager.getSnapshot();

    assert(snapshot->has("app.name"));
    assert(!snapshot->has("app.nonexistent"));
    assert(snapshot->getString("app.name") == "test-app");
    assert(snapshot->getDouble("app.version") == 1.0);
    assert(snapshot->getBool("app.debug") == true);
    assert(snapshot->getInt("database.port") == 5432);

    auto tags = snapshot->getStringList("app.tags");
    assert(tags.size() == 2);
    assert(tags[0] == "tag1");
    assert(tags[1] == "tag2");

    auto dbSection = snapshot->getSection("database");
    assert(dbSection != nullptr);
    assert(dbSection->getString("host") == "localhost");
    assert(dbSection->getInt("port") == 5432);

    auto keys = snapshot->getKeys();
    assert(keys.size() == 3);

    removeTempFile(commonPath);
    system(("rmdir " + tempDir).c_str());

    std::cout << "test_snapshot_operations passed" << std::endl;
}

int main() {
    test_load_basic();
    test_load_missing_common();
    test_snapshot_operations();
    return 0;
}
