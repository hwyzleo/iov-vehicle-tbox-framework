#include "config.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <atomic>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

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

// ===================== CR-008: 完整配置树快照与 toYaml() 导出测试 =====================

// 递归深度比较两个 YAML::Node：节点类型、map key 集合与值、
// sequence 长度/顺序/元素、scalar 值。用于验证 toYaml() 无损往返。
// map 采用 key 集合比较（避免 operator[] 产生 zombie 节点）；sequence 按索引比较以校验顺序。
static bool nodesEqual(const YAML::Node& a, const YAML::Node& b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    if (a.Type() != b.Type()) return false;
    switch (a.Type()) {
        case YAML::NodeType::Undefined:
        case YAML::NodeType::Null:
            return true;
        case YAML::NodeType::Scalar:
            return a.Scalar() == b.Scalar();
        case YAML::NodeType::Sequence: {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (!nodesEqual(a[i], b[i])) return false;
            }
            return true;
        }
        case YAML::NodeType::Map: {
            if (a.size() != b.size()) return false;
            // 注意：yaml-cpp 迭代器 it->second 返回临时值，不可取地址存储；
            // 必须按值拷贝 YAML::Node（内部为共享句柄，拷贝廉价且稳定）。
            std::map<std::string, YAML::Node> bmap;
            for (auto it = b.begin(); it != b.end(); ++it) {
                bmap[it->first.as<std::string>()] = it->second;
            }
            for (auto it = a.begin(); it != a.end(); ++it) {
                std::string key = it->first.as<std::string>();
                auto bit = bmap.find(key);
                if (bit == bmap.end()) return false;
                if (!nodesEqual(it->second, bit->second)) return false;
            }
            return true;
        }
    }
    return false;
}

static std::string makeTempConfigDir(const std::string& tag, const std::string& commonContent) {
    std::string dir = "/tmp/test_cr008_" + tag;
    system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    createTempFile(dir + "/common.yaml", commonContent);
    return dir;
}

static std::string makeTempConfigDirWithSvc(const std::string& tag,
                                            const std::string& commonContent,
                                            const std::string& svcName,
                                            const std::string& svcContent) {
    std::string dir = "/tmp/test_cr008_" + tag;
    std::string confd = dir + "/conf.d";
    system(("rm -rf " + dir + " && mkdir -p " + confd).c_str());
    createTempFile(dir + "/common.yaml", commonContent);
    createTempFile(confd + "/" + svcName + ".yaml", svcContent);
    return dir;
}

// 9.1 结构完整性：覆盖 scalar seq / seq-of-maps / 嵌套 seq / map 嵌套 seq /
// 空 seq / 空 map / 显式 null / 深层混合，断言源合并树与 toYaml() 返回树深度相等
void test_toyaml_structural_integrity() {
    std::string content = R"(
common:
  log:
    level: info
scalar_seq:
  - a
  - b
  - c
seq_of_maps:
  - id: 1
    name: first
  - id: 2
    name: second
nested_seq:
  - - 1
    - 2
  - - 3
    - 4
map_with_seq:
  items:
    - x
    - y
  nested:
    deep:
      list:
        - 10
        - 20
empty_seq: []
empty_map: {}
explicit_null: null
mixed:
  level1:
    list:
      - key: val1
        nums:
          - 1
          - 2
      - key: val2
        flag: true
    scalar: hello
)";
    std::string dir = makeTempConfigDir("struct", content);
    ConfigManager& manager = ConfigManager::instance();
    ConfigError error = manager.load("test", dir);
    assert(error == ConfigError::kOk);

    YAML::Node expected = YAML::Load(content);
    YAML::Node actual = manager.toYaml();

    assert(nodesEqual(expected, actual));
    // 关键结构点逐一断言
    assert(actual["scalar_seq"].IsSequence() && actual["scalar_seq"].size() == 3);
    assert(actual["seq_of_maps"].IsSequence() && actual["seq_of_maps"].size() == 2);
    assert(actual["seq_of_maps"][0]["id"].as<int>() == 1);
    assert(actual["seq_of_maps"][1]["name"].as<std::string>() == "second");
    assert(actual["nested_seq"].IsSequence() && actual["nested_seq"].size() == 2);
    assert(actual["nested_seq"][0].IsSequence() && actual["nested_seq"][0].size() == 2);
    assert(actual["empty_seq"].IsSequence() && actual["empty_seq"].size() == 0);
    assert(actual["empty_map"].IsMap() && actual["empty_map"].size() == 0);
    assert(actual["explicit_null"].IsNull());
    assert(actual["mixed"]["level1"]["list"][1]["flag"].as<bool>() == true);

    system(("rm -rf " + dir).c_str());
    std::cout << "test_toyaml_structural_integrity passed" << std::endl;
}

// 9.2 合并回归：array 后层整体替换不拼接、map 未覆盖 key 保留、scalar 覆盖、三层优先级
void test_toyaml_merge_regression() {
    std::string common = R"(
common:
  log:
    level: info
tsp:
  device-sn: common-sn
  subscriptions:
    - route_id: old.uplink
      direction: UP
keep_map:
  a: 1
  b: 2
)";
    std::string svc = R"(
tsp:
  device-sn: svc-sn
  subscriptions:
    - route_id: new.downlink
      direction: DOWN
keep_map:
  c: 3
)";
    std::string dir = makeTempConfigDirWithSvc("merge", common, "test", svc);
    ConfigManager& manager = ConfigManager::instance();
    ConfigError error = manager.load("test", dir);
    assert(error == ConfigError::kOk);

    YAML::Node root = manager.toYaml();
    // scalar 后层覆盖
    assert(root["tsp"]["device-sn"].as<std::string>() == "svc-sn");
    // array 后层整体替换，不拼接元素
    assert(root["tsp"]["subscriptions"].IsSequence());
    assert(root["tsp"]["subscriptions"].size() == 1);
    assert(root["tsp"]["subscriptions"][0]["route_id"].as<std::string>() == "new.downlink");
    // map 未覆盖 key 保留
    assert(root["keep_map"]["a"].as<int>() == 1);
    assert(root["keep_map"]["b"].as<int>() == 2);
    assert(root["keep_map"]["c"].as<int>() == 3);
    // getter 一致
    auto snap = manager.getSnapshot();
    assert(snap->getString("tsp.device-sn") == "svc-sn");

    system(("rm -rf " + dir).c_str());
    std::cout << "test_toyaml_merge_regression passed" << std::endl;
}

// 9.3 Getter 回归：sequence-of-maps 不改变 getter 失败/空返回语义
void test_toyaml_getter_regression_seq_of_maps() {
    std::string content = R"(
common:
  log:
    level: info
seq_of_maps:
  - id: 1
    name: first
plain_scalar: hello
plain_seq:
  - one
  - two
)";
    std::string dir = makeTempConfigDir("getter", content);
    ConfigManager& manager = ConfigManager::instance();
    assert(manager.load("test", dir) == ConfigError::kOk);
    auto snap = manager.getSnapshot();

    // seq_of_maps 是 sequence，getSection 应返回 nullptr（非 map）
    assert(snap->getSection("seq_of_maps") == nullptr);
    // getStringList 仅返回标量序列，seq_of_maps 非标量序列 -> 空
    assert(snap->getStringList("seq_of_maps").empty());
    // has 仍可判定 key 存在
    assert(snap->has("seq_of_maps"));
    // 标量与标量序列 getter 正常
    assert(snap->getString("plain_scalar") == "hello");
    auto ps = snap->getStringList("plain_seq");
    assert(ps.size() == 2 && ps[0] == "one" && ps[1] == "two");
    // getKeys 包含 seq_of_maps
    auto keys = snap->getKeys();
    bool found = false;
    for (const auto& k : keys) if (k == "seq_of_maps") found = true;
    assert(found);

    system(("rm -rf " + dir).c_str());
    std::cout << "test_toyaml_getter_regression_seq_of_maps passed" << std::endl;
}

// 9.4 不可变隔离：修改 toYaml() 返回树不影响快照与第二次返回
void test_toyaml_isolation() {
    std::string content = R"(
common:
  log:
    level: info
tsp:
  device-sn: original
  subscriptions:
    - route_id: fota.uplink
      direction: UP
)";
    std::string dir = makeTempConfigDir("isolation", content);
    ConfigManager& manager = ConfigManager::instance();
    assert(manager.load("test", dir) == ConfigError::kOk);

    YAML::Node first = manager.toYaml();
    // 修改首次返回树
    first["common"]["log"]["level"] = "modified";
    first["tsp"]["device-sn"] = "tampered";
    first["tsp"]["subscriptions"].push_back("injected");
    first["new_key"] = "injected";

    // 快照 getter 不受影响
    auto snap = manager.getSnapshot();
    assert(snap->getString("common.log.level") == "info");
    assert(snap->getString("tsp.device-sn") == "original");
    // 第二次返回树不受影响
    YAML::Node second = manager.toYaml();
    assert(second["common"]["log"]["level"].as<std::string>() == "info");
    assert(second["tsp"]["device-sn"].as<std::string>() == "original");
    assert(second["tsp"]["subscriptions"].size() == 1);
    assert(!second["new_key"]);

    system(("rm -rf " + dir).c_str());
    std::cout << "test_toyaml_isolation passed" << std::endl;
}

// 9.4 并发：多线程并发 getter 与 toYaml()，结果一致且无数据竞争
void test_toyaml_concurrency() {
    std::string content = R"(
common:
  log:
    level: info
tsp:
  device-sn: VIN123
  subscriptions:
    - route_id: fota.uplink
      direction: UP
    - route_id: fota.downlink
      direction: DOWN
)";
    std::string dir = makeTempConfigDir("concurrency", content);
    ConfigManager& manager = ConfigManager::instance();
    assert(manager.load("test", dir) == ConfigError::kOk);

    const int N = 8;
    const int ITERS = 200;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < N; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < ITERS; ++i) {
                YAML::Node y = manager.toYaml();
                if (!y["tsp"]["subscriptions"].IsSequence() ||
                    y["tsp"]["subscriptions"].size() != 2) {
                    failures.fetch_add(1);
                    continue;
                }
                auto snap = manager.getSnapshot();
                if (snap->getString("tsp.device-sn") != "VIN123") {
                    failures.fetch_add(1);
                }
            }
        });
    }
    for (auto& th : threads) th.join();
    assert(failures.load() == 0);

    system(("rm -rf " + dir).c_str());
    std::cout << "test_toyaml_concurrency passed" << std::endl;
}

// 9.5 TSP 集成场景模拟：fota.uplink + fota.downlink 两条订阅，验证 toYaml() 恢复 sequence-of-maps
void test_toyaml_tsp_scenario() {
    std::string content = R"(
common:
  log:
    level: info
tsp:
  device-sn: VIN12345
  subscriptions:
    - route_id: fota.uplink
      topic_template: "tbox/{sn}/fota/uplink"
      direction: UP
      qos: 1
      target: cloud
      mandatory: false
    - route_id: fota.downlink
      topic_template: "cloud/{sn}/fota/downlink"
      direction: DOWN
      qos: 1
      target: tbox
      mandatory: true
)";
    std::string dir = makeTempConfigDir("tsp", content);
    ConfigManager& manager = ConfigManager::instance();
    assert(manager.load("test", dir) == ConfigError::kOk);

    YAML::Node subs = manager.toYaml()["tsp"]["subscriptions"];
    assert(subs.IsSequence());
    assert(subs.size() == 2);
    // fota.uplink
    assert(subs[0]["route_id"].as<std::string>() == "fota.uplink");
    assert(subs[0]["topic_template"].as<std::string>() == "tbox/{sn}/fota/uplink");
    assert(subs[0]["direction"].as<std::string>() == "UP");
    assert(subs[0]["qos"].as<int>() == 1);
    assert(subs[0]["target"].as<std::string>() == "cloud");
    assert(subs[0]["mandatory"].as<bool>() == false);
    // fota.downlink
    assert(subs[1]["route_id"].as<std::string>() == "fota.downlink");
    assert(subs[1]["direction"].as<std::string>() == "DOWN");
    assert(subs[1]["mandatory"].as<bool>() == true);
    // 标量 getter 仍正确（证明分层加载与合并结果存在）
    assert(manager.getSnapshot()->getString("tsp.device-sn") == "VIN12345");

    system(("rm -rf " + dir).c_str());
    std::cout << "test_toyaml_tsp_scenario passed" << std::endl;
}

// 9.6 性能记录：小/中/大配置 toYaml() 耗时（仅记录，不设硬门槛，不以牺牲隔离换性能）
void test_toyaml_performance() {
    auto buildConfig = [](int nKeys) {
        std::string c = "common:\n  log:\n    level: info\ntsp:\n  device-sn: perf-sn\n  subscriptions:\n";
        int nSubs = std::max(1, nKeys / 10);
        for (int i = 0; i < nSubs; ++i) {
            c += "    - route_id: route." + std::to_string(i) + "\n";
            c += "      direction: UP\n      qos: 1\n";
        }
        for (int i = 0; i < nKeys; ++i) {
            c += "key" + std::to_string(i) + ":\n";
            c += "  idx: " + std::to_string(i) + "\n";
            c += "  tags:\n    - t" + std::to_string(i % 3) + "\n    - t" + std::to_string((i + 1) % 3) + "\n";
        }
        return c;
    };

    ConfigManager& manager = ConfigManager::instance();
    for (int size : {10, 100, 500}) {
        std::string content = buildConfig(size);
        std::string dir = "/tmp/test_cr008_perf_" + std::to_string(size);
        system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
        createTempFile(dir + "/common.yaml", content);
        assert(manager.load("test", dir) == ConfigError::kOk);

        const int LOOPS = 100;
        manager.toYaml();  // warmup
        std::vector<double> times;
        for (int i = 0; i < LOOPS; ++i) {
            auto t0 = std::chrono::high_resolution_clock::now();
            YAML::Node y = manager.toYaml();
            auto t1 = std::chrono::high_resolution_clock::now();
            times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            (void)y;
        }
        std::sort(times.begin(), times.end());
        double p50 = times[times.size() / 2];
        double p95 = times[(size_t)(times.size() * 0.95)];
        std::cout << "  perf size=" << size << " toYaml() P50=" << p50 << "us P95=" << p95 << "us" << std::endl;
        system(("rm -rf " + dir).c_str());
    }
    std::cout << "test_toyaml_performance passed" << std::endl;
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
    // CR-008: 完整配置树快照与 toYaml() 导出
    test_toyaml_structural_integrity();
    test_toyaml_merge_regression();
    test_toyaml_getter_regression_seq_of_maps();
    test_toyaml_isolation();
    test_toyaml_concurrency();
    test_toyaml_tsp_scenario();
    test_toyaml_performance();
    return 0;
}
