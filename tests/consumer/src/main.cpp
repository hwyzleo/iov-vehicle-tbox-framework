// tests/consumer/src/main.cpp
// TBOX-FW-DSN-CR-010 installed-package consumer smoke test.
//
// 仅通过 find_package(TboxFramework) 消费已安装的 framework，验证各组件
// target 链接与公共 API 可用。不引用 framework 源码目录或私有头文件。
// 覆盖 config/store/log/ipc/hash/Application 最小回环，使用临时目录与 socket。
#include "hash.h"
#include "config.h"
#include "store.h"
#include "log.h"
#include "ipc.h"
#include "ipc_types.h"
#include "application.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <unistd.h>
#include <sys/stat.h>

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL: %s\n", msg);                            \
            ++g_failures;                                                  \
        } else {                                                           \
            printf("ok: %s\n", msg);                                       \
        }                                                                  \
    } while (0)

static void write_file(const std::string& path, const std::string& content) {
    FILE* f = fopen(path.c_str(), "w");
    assert(f);
    fwrite(content.data(), 1, content.size(), f);
    fclose(f);
}

static void mkdir_p(const std::string& path) {
    mkdir(path.c_str(), 0755);
    mkdir((path + "/conf.d").c_str(), 0755);
}

// ===================== hash =====================
static void test_hash() {
    using namespace tbox::fw::hash;
    std::string abc = sha256_hex(std::string_view("abc"));
    CHECK(abc == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "hash sha256_hex(abc) == NIST vector");
    std::string empty = sha256_hex(std::string_view(""));
    CHECK(empty == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "hash sha256_hex(\"\") == empty-input vector");
}

// ===================== config =====================
static void test_config() {
    using namespace hwyz::config;
    const std::string tmp = "/tmp/fw_consumer_config";
    mkdir_p(tmp);
    write_file(tmp + "/common.yaml",
               "common:\n  log:\n    level: INFO\n    console:\n      enabled: true\n");
    write_file(tmp + "/conf.d/consumer.yaml", "consumer:\n  name: smoke\n");

    ConfigManager& mgr = ConfigManager::instance();
    ConfigError err = mgr.load("consumer", tmp);
    CHECK(err == ConfigError::kOk, "config load(consumer, tmp)");

    auto snap = mgr.getSnapshot();
    CHECK(snap != nullptr, "config getSnapshot()");
    if (snap) {
        std::string lvl = snap->getString("common.log.level", "MISSING");
        CHECK(lvl == "INFO", "config getString(common.log.level)==INFO");
        std::string name = snap->getString("consumer.name", "MISSING");
        CHECK(name == "smoke", "config getString(consumer.name)==smoke");
    }
}

// ===================== store =====================
static void test_store() {
    using namespace hwyz::store;
    const std::string root = "/tmp/fw_consumer_store";
    Store store = Store::open("consumer", root);
    CHECK(store.isReady(), "store open(consumer, tmp)");

    store.save<std::string>("consumer.greeting", "hello framework");
    std::string v = store.load<std::string>("consumer.greeting");
    CHECK(v == "hello framework", "store save/load<string> roundtrip");

    int n = store.loadOr<int>("consumer.missing", 7);
    CHECK(n == 7, "store loadOr default");

    store.cleanup();
}

// ===================== log =====================
static void test_log() {
    using namespace tbox::fw::log;
    LogConfig cfg;  // 默认: console + INFO + async
    InitResult r = Logger::init("framework_consumer", cfg);
    CHECK(r.error == LogError::kOk, "log init(consumer, default config)");

    Logger log = Logger::get("smoke");
    log.info("consumer.smoke.ok", "framework-log structured event from consumer");
    log.flush();
    Logger::shutdown();
}

// ===================== ipc =====================
static void test_ipc() {
    using namespace tbox::fw::ipc;
    const char* sock = "/tmp/fw_consumer_ipc.sock";
    ::unlink(sock);

    Server server(sock);
    auto handler = [](uint32_t method_id, std::string_view params, int /*fd*/) -> std::string {
        if (method_id == 1) return std::string(params);
        return R"({"error":"unknown"})";
    };
    CHECK(server.start(handler), "ipc server start(temp socket)");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    Client client(sock);
    auto [status, resp] = client.call(1, R"({"ping":true})");
    CHECK(status == 0, "ipc client call status==0");
    CHECK(resp == R"({"ping":true})", "ipc client call echo response");

    server.stop();
    ::unlink(sock);
}

// ===================== application =====================
static void test_application() {
    // 静态校验接口可用（不触发 run()，避免依赖 /etc/tbox 配置与信号环境）。
    std::string v = hwyz::Application::validateSignalSets(
        {SIGINT, SIGTERM}, {SIGSEGV, SIGABRT}, {SIGPIPE});
    CHECK(v.empty(), "application validateSignalSets(default sets)==ok");

    std::string bad = hwyz::Application::validateSignalSets(
        {SIGKILL}, {}, {});
    CHECK(!bad.empty(), "application validateSignalSets(SIGKILL)==rejected");

    // 最小子类实例化，验证 vtable/符号链接（不 run）。
    struct SmokeApp : public hwyz::Application {
        std::string getServiceName() const override { return "framework_consumer"; }
        bool initialize() override { return true; }
        void cleanup() override {}
        int execute() override { return 0; }
    };
    SmokeApp app;
    CHECK(app.getServiceName() == "framework_consumer", "application subclass instantiate");
}

int main() {
    test_hash();
    test_config();
    test_store();
    test_log();
    test_ipc();
    test_application();

    printf("\n========================================\n");
    printf("consumer smoke: %d checks, %d failure(s)\n", g_checks, g_failures);
    printf("========================================\n");
    return g_failures == 0 ? 0 : 1;
}
