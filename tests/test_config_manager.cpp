#include "config.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <unistd.h>
#include <cstdlib>

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

void test_load_with_project_layer() {
    std::string tempDir = "/tmp/test_config_project";
    std::string commonPath = tempDir + "/common.yaml";
    std::string serviceDir = tempDir + "/conf.d/";
    std::string servicePath = serviceDir + "test.yaml";
    // ./config/ 相对于当前工作目录
    std::string projectDir = tempDir + "/config/";
    std::string projectPath = projectDir + "test.yaml";

    system(("mkdir -p " + serviceDir).c_str());
    system(("mkdir -p " + projectDir).c_str());

    createTempFile(commonPath, R"(
log:
  level: info
server:
  host: localhost
  port: 8080
)");

    createTempFile(servicePath, R"(
server:
  port: 9090
)");

    createTempFile(projectPath, R"(
log:
  level: debug
server:
  timeout: 30
)");

    // 切换到 tempDir 作为工作目录，这样 ./config/ 就指向 projectDir
    char* origDir = getcwd(nullptr, 0);
    chdir(tempDir.c_str());

    ConfigManager& manager = ConfigManager::instance();
    ConfigError error = manager.load("test", tempDir);

    assert(error == ConfigError::kOk);
    assert(manager.isLoaded());

    auto snapshot = manager.getSnapshot();
    assert(snapshot != nullptr);

    // common 的 log.level = info，被 project 覆盖为 debug
    assert(snapshot->getString("log.level") == "debug");
    // service 的 server.port = 9090
    assert(snapshot->getInt("server.port") == 9090);
    // project 新增的 server.timeout
    assert(snapshot->getInt("server.timeout") == 30);
    // common 的 server.host 保留
    assert(snapshot->getString("server.host") == "localhost");

    // 恢复目录
    chdir(origDir);
    free(origDir);

    removeTempFile(commonPath);
    removeTempFile(servicePath);
    removeTempFile(projectPath);
    system(("rmdir " + serviceDir).c_str());
    system(("rmdir " + projectDir).c_str());
    system(("rmdir " + tempDir).c_str());

    std::cout << "test_load_with_project_layer passed" << std::endl;
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
    test_load_with_project_layer();
    test_load_missing_common();
    test_snapshot_operations();
    return 0;
}
