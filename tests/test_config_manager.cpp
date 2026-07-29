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
common:
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

    assert(snapshot->getString("common.log.level") == "info");
    assert(snapshot->getString("common.log.format") == "json");
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
common:
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
common:
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

    // common 的 common.log.level = info，被 project 覆盖为 debug
    assert(snapshot->getString("common.log.level") == "debug");
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
common:
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

void test_load_with_project_common() {
    std::string tempDir = "/tmp/test_config_proj_common";
    std::string rootCommonPath = tempDir + "/common.yaml";
    std::string projectDir = tempDir + "/config/";
    std::string projectCommonPath = projectDir + "common.yaml";

    system(("mkdir -p " + projectDir).c_str());

    createTempFile(rootCommonPath, R"(
common:
  log:
    level: info
server:
  host: root-host
)");

    createTempFile(projectCommonPath, R"(
common:
  log:
    level: debug
server:
  timeout: 30
)");

    // 切换到 tempDir，使 ./config/common.yaml 指向 projectCommonPath
    char* origDir = getcwd(nullptr, 0);
    chdir(tempDir.c_str());

    ConfigManager& manager = ConfigManager::instance();
    ConfigError error = manager.load("test", tempDir);

    assert(error == ConfigError::kOk);
    assert(manager.isLoaded());

    auto snapshot = manager.getSnapshot();
    assert(snapshot != nullptr);

    // project common 覆盖 root common 的 common.log.level
    assert(snapshot->getString("common.log.level") == "debug");
    // root common 的 server.host 保留（project common 未覆盖）
    assert(snapshot->getString("server.host") == "root-host");
    // project common 新增的 server.timeout
    assert(snapshot->getInt("server.timeout") == 30);

    chdir(origDir);
    free(origDir);

    removeTempFile(rootCommonPath);
    removeTempFile(projectCommonPath);
    system(("rmdir " + projectDir).c_str());
    system(("rmdir " + tempDir).c_str());

    std::cout << "test_load_with_project_common passed" << std::endl;
}

void test_load_project_common_only() {
    // 没有 root common，仅存在 ./config/common.yaml，应当加载成功
    std::string tempDir = "/tmp/test_config_proj_common_only";
    std::string projectDir = tempDir + "/config/";
    std::string projectCommonPath = projectDir + "common.yaml";

    system(("mkdir -p " + projectDir).c_str());

    // 注意：不创建 tempDir/common.yaml
    createTempFile(projectCommonPath, R"(
common:
  log:
    level: warn
)");

    char* origDir = getcwd(nullptr, 0);
    chdir(tempDir.c_str());

    ConfigManager& manager = ConfigManager::instance();
    ConfigError error = manager.load("test", tempDir);

    assert(error == ConfigError::kOk);
    assert(manager.isLoaded());

    auto snapshot = manager.getSnapshot();
    assert(snapshot != nullptr);
    assert(snapshot->getString("common.log.level") == "warn");

    chdir(origDir);
    free(origDir);

    removeTempFile(projectCommonPath);
    system(("rmdir " + projectDir).c_str());
    system(("rmdir " + tempDir).c_str());

    std::cout << "test_load_project_common_only passed" << std::endl;
}

void test_load_with_common_log() {
    // 规范位置：日志配置位于 common.log
    std::string tempDir = "/tmp/test_config_common_log";
    std::string commonPath = tempDir + "/common.yaml";

    system(("mkdir -p " + tempDir).c_str());

    createTempFile(commonPath, R"(
common:
  log:
    level: debug
    file: /var/log/tbox/test.log
  store:
    root: /var/tbox
)");

    ConfigManager& manager = ConfigManager::instance();
    ConfigError error = manager.load("test", tempDir);

    assert(error == ConfigError::kOk);
    assert(manager.isLoaded());

    auto snapshot = manager.getSnapshot();
    assert(snapshot != nullptr);
    assert(snapshot->getString("common.log.level") == "debug");
    assert(snapshot->getString("common.store.root") == "/var/tbox");

    removeTempFile(commonPath);
    system(("rmdir " + tempDir).c_str());

    std::cout << "test_load_with_common_log passed" << std::endl;
}

void test_load_missing_log_config() {
    // common.log 与顶层 log 都不存在时，校验必须失败
    std::string tempDir = "/tmp/test_config_no_log";
    std::string commonPath = tempDir + "/common.yaml";

    system(("mkdir -p " + tempDir).c_str());

    createTempFile(commonPath, R"(
common:
  store:
    root: /var/tbox
)");

    ConfigManager& manager = ConfigManager::instance();
    ConfigError error = manager.load("test", tempDir);

    assert(error == ConfigError::kValidationFailed);
    assert(!manager.isLoaded());
    assert(manager.getLastError().path == "common.log");

    removeTempFile(commonPath);
    system(("rmdir " + tempDir).c_str());

    std::cout << "test_load_missing_log_config passed" << std::endl;
}

int main() {
    test_load_basic();
    test_load_with_project_layer();
    test_load_with_project_common();
    test_load_project_common_only();
    test_load_with_common_log();
    test_load_missing_log_config();
    test_load_missing_common();
    test_snapshot_operations();
    return 0;
}
