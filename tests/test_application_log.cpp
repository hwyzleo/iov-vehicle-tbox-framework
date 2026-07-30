//
// test_application_log.cpp
//
// TBOX-FW-DSN-CR-007: Application 日志相关单测。
// 覆盖 buildLogConfig（完整/缺省/非法/旧 schema）、strict true/false、
// spdlog adapter 不产生第二套 sink 且无重复行、adapter 幂等。
//

#include "application.h"
#include "log.h"
#include "log_types.h"
#include "log/log_config_adapter.h"
#include "log/log_spdlog_adapter.h"

#include "spdlog/spdlog.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace tbox::fw::log;

// 可暴露 protected 成员、可注入服务名/根目录的测试子类
class TestApp : public hwyz::Application {
public:
    using Application::useFrameworkLog;
    using Application::buildLogConfig;
    using Application::setup_logging;
    using Application::load_config;

    void setServiceName(const std::string &s) { svc_ = s; }
    std::string getServiceName() const override { return svc_; }
    void setConfigRoots(const std::vector<std::string> &r) { roots_ = r; }
    std::vector<std::string> getConfigRoots() const override { return roots_; }

    int execute() override { return 0; }
    bool initialize() override { return true; }
    void cleanup() override {}

private:
    std::string svc_ = "testapp";
    std::vector<std::string> roots_ = {"./"};
};

// 创建临时目录并写入 common.yaml，返回目录路径
static std::string writeTempConfig(const std::string &yamlContent) {
    char tmpl[] = "/tmp/tbox_apptest_XXXXXX";
    char *dir = mkdtemp(tmpl);
    assert(dir != nullptr);
    std::string dirStr(dir);
    std::ofstream f(dirStr + "/common.yaml");
    f << yamlContent;
    f.close();
    return dirStr + "/";
}

static void resetLogger() {
    Logger::shutdown();
    SpdlogAdapter::uninstall();
}

// ---------- buildLogConfig ----------

void test_buildLogConfig_full() {
    std::string dir = writeTempConfig(R"(
common:
  log:
    schema_version: 1
    level: WARN
    strict: true
    format: json
    console:
      enabled: true
    file:
      enabled: false
    async:
      enabled: false
      queue_size: 1024
)");
    TestApp app;
    app.setConfigRoots({dir});
    assert(app.load_config());
    LogConfig c = app.buildLogConfig();
    assert(c.schema_version == 1);
    assert(c.level == LogLevel::kWarn);
    assert(c.strict == true);
    assert(c.format == "json");
    assert(c.console_config.enabled == true);
    assert(c.async_config.enabled == false);
    assert(c.async_config.queue_size == 1024);
    std::cout << "  [PASS] test_buildLogConfig_full" << std::endl;
}

void test_buildLogConfig_missing_defaults() {
    // ConfigManager 校验要求 common.log 必填；提供最小 common.log，其余字段缺省。
    std::string dir = writeTempConfig(R"(
common:
  log:
    schema_version: 1
)" + std::string("\n"));
    TestApp app;
    app.setConfigRoots({dir});
    assert(app.load_config());
    LogConfig c = app.buildLogConfig();
    // 仅提供 schema_version，其余使用 LogConfig 默认值
    assert(c.schema_version == 1);
    assert(c.level == LogLevel::kInfo);
    assert(c.strict == false);
    assert(c.console_config.enabled == true);
    assert(c.file_config.enabled == false);
    std::cout << "  [PASS] test_buildLogConfig_missing_defaults" << std::endl;
}

void test_buildLogConfig_invalid_preserved() {
    // schema_version=2 非法；buildLogConfig 应保留原值（不静默降级），
    // 交由 setup_logging 的 validate 在 strict 模式触发 fail-closed。
    std::string dir = writeTempConfig(R"(
common:
  log:
    schema_version: 2
    strict: true
)");
    TestApp app;
    app.setConfigRoots({dir});
    assert(app.load_config());
    LogConfig c = app.buildLogConfig();
    assert(c.schema_version == 2);
    assert(c.strict == true);
    // validate 应失败
    LogErrorInfo verr = LogConfigAdapter::validate(c);
    assert(verr.code == LogError::kConfigInvalid);
    std::cout << "  [PASS] test_buildLogConfig_invalid_preserved" << std::endl;
}

void test_buildLogConfig_legacy_schema_ignored() {
    // 旧 schema（common.log.type/path）不被 buildLogConfig 读取
    std::string dir = writeTempConfig(R"(
common:
  log:
    type: file
    path: /tmp/legacy.log
    schema_version: 1
    level: INFO
)");
    TestApp app;
    app.setConfigRoots({dir});
    assert(app.load_config());
    LogConfig c = app.buildLogConfig();
    assert(c.schema_version == 1);
    assert(c.level == LogLevel::kInfo);
    // 旧 type/path 不映射到 LogConfig 字段；console 默认开启
    assert(c.console_config.enabled == true);
    assert(c.file_config.enabled == false);
    std::cout << "  [PASS] test_buildLogConfig_legacy_schema_ignored" << std::endl;
}

// ---------- setup_logging strict ----------

void test_setup_logging_strict_failclosed() {
    resetLogger();
    std::string dir = writeTempConfig(R"(
common:
  log:
    schema_version: 2
    strict: true
)");
    TestApp app;
    app.setConfigRoots({dir});
    assert(app.load_config());
    bool ok = app.setup_logging();
    assert(!ok);  // strict + 非法配置 -> fail-closed
    resetLogger();
    std::cout << "  [PASS] test_setup_logging_strict_failclosed" << std::endl;
}

void test_setup_logging_nonstrict_degrade() {
    resetLogger();
    std::string dir = writeTempConfig(R"(
common:
  log:
    schema_version: 2
    strict: false
)");
    TestApp app;
    app.setConfigRoots({dir});
    assert(app.load_config());
    bool ok = app.setup_logging();
    assert(ok);  // 非 strict -> 降级到默认 console+INFO，继续
    resetLogger();
    std::cout << "  [PASS] test_setup_logging_nonstrict_degrade" << std::endl;
}

void test_setup_logging_valid() {
    resetLogger();
    std::string dir = writeTempConfig(R"(
common:
  log:
    schema_version: 1
    level: INFO
    strict: false
    console:
      enabled: true
    async:
      enabled: false
)");
    TestApp app;
    app.setConfigRoots({dir});
    assert(app.load_config());
    bool ok = app.setup_logging();
    assert(ok);
    assert(SpdlogAdapter::isInstalled());
    resetLogger();
    std::cout << "  [PASS] test_setup_logging_valid" << std::endl;
}

// ---------- spdlog adapter: 同一 sink、无重复行 ----------

// 读取文件全部内容
static std::string readFile(const std::string &path) {
    std::ifstream f(path);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

static size_t countOccurrence(const std::string &hay, const std::string &needle) {
    size_t cnt = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        ++cnt;
        pos += needle.size();
    }
    return cnt;
}

void test_spdlog_adapter_same_sink_no_duplicate() {
    resetLogger();
    char tmpl[] = "/tmp/tbox_adaptlog_XXXXXX";
    char *dir = mkdtemp(tmpl);
    assert(dir != nullptr);
    std::string dirStr(dir);

    // 初始化 framework-log：仅 file sink，async 关闭（确定性 flush），standard 格式
    LogConfig config;
    config.schema_version = 1;
    config.level = LogLevel::kTrace;
    config.strict = false;
    config.format = "standard";
    config.async_config.enabled = false;
    config.console_config.enabled = false;
    config.file_config.enabled = true;
    config.file_config.root = dirStr;

    InitResult r = Logger::init("testapp", config);
    assert(r.error == LogError::kOk);

    SpdlogAdapter::installAsDefault();

    Logger logger = Logger::get("main");
    logger.info("fw.event", "fw-message-payload");
    // 遗留 spdlog 调用应经 adapter 进入同一 file sink
    spdlog::info("spdlog-message-payload");
    spdlog::warn("spdlog-warn-payload");

    logger.flush();

    std::string filePath = dirStr + "/testapp/testapp_0.log";
    std::string content = readFile(filePath);

    // 两条消息各出现恰好一次（同一 sink，无重复）
    assert(countOccurrence(content, "fw-message-payload") == 1);
    assert(countOccurrence(content, "spdlog-message-payload") == 1);
    assert(countOccurrence(content, "spdlog-warn-payload") == 1);

    resetLogger();
    std::cout << "  [PASS] test_spdlog_adapter_same_sink_no_duplicate" << std::endl;
}

void test_spdlog_adapter_idempotent() {
    resetLogger();
    LogConfig config = LogConfigAdapter::getDefaultConfig();
    config.async_config.enabled = false;
    config.console_config.enabled = false;
    config.file_config.enabled = false;
    Logger::init("testapp", config);

    SpdlogAdapter::installAsDefault();
    assert(SpdlogAdapter::isInstalled());
    // 重复安装不应创建第二套 sink / 覆盖默认 logger
    SpdlogAdapter::installAsDefault();
    SpdlogAdapter::installAsDefault();
    assert(SpdlogAdapter::isInstalled());

    // 遗留 spdlog 调用仍可工作（不崩溃）
    spdlog::info("idempotent-ok");

    resetLogger();
    std::cout << "  [PASS] test_spdlog_adapter_idempotent" << std::endl;
}

int main() {
    std::cout << "Running application log tests..." << std::endl;
    test_buildLogConfig_full();
    test_buildLogConfig_missing_defaults();
    test_buildLogConfig_invalid_preserved();
    test_buildLogConfig_legacy_schema_ignored();
    test_setup_logging_strict_failclosed();
    test_setup_logging_nonstrict_degrade();
    test_setup_logging_valid();
    test_spdlog_adapter_same_sink_no_duplicate();
    test_spdlog_adapter_idempotent();
    std::cout << "All application log tests passed!" << std::endl;
    return 0;
}
