//
// test_application_integration.cpp
//
// TBOX-FW-DSN-CR-007: Application 端到端集成测试。
// 覆盖最小 daemon 生命周期顺序（init->execute->cleanup）、cleanup 恰好一次、
// requestShutdown 触发 execute(waitForShutdown) 优雅退出、run() 输出无裸 std::cout。
//

#include "application.h"
#include "log.h"
#include "log_types.h"
#include "log/log_spdlog_adapter.h"

#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace tbox::fw::log;

class DaemonApp : public hwyz::Application {
public:
    // 暴露 protected 以便测试驱动
    using Application::requestShutdown;

    std::vector<std::string> order;
    int cleanup_count = 0;
    int execute_rc = 0;

    void setServiceName(const std::string &s) { svc_ = s; }
    std::string getServiceName() const override { return svc_; }
    void setConfigRoots(const std::vector<std::string> &r) { roots_ = r; }
    std::vector<std::string> getConfigRoots() const override { return roots_; }

    bool initialize() override {
        order.push_back("init");
        return true;
    }

    int execute() override {
        order.push_back("execute");
        waitForShutdown(std::chrono::milliseconds(20));
        return execute_rc;
    }

    void cleanup() override {
        ++cleanup_count;
        order.push_back("cleanup");
    }

private:
    std::string svc_ = "testapp";
    std::vector<std::string> roots_ = {"./"};
};

static std::string writeTempConfig(const std::string &yamlContent) {
    char tmpl[] = "/tmp/tbox_integtest_XXXXXX";
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

// 生命周期顺序 + cleanup 恰好一次 + requestShutdown 触发优雅退出
void test_lifecycle_order_and_cleanup_once() {
    resetLogger();
    std::string dir = writeTempConfig(R"(
common:
  log:
    schema_version: 1
    level: INFO
    strict: false
    console:
      enabled: false
    async:
      enabled: false
)");

    DaemonApp app;
    app.setConfigRoots({dir});
    app.execute_rc = 0;

    std::thread killer([&app]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        app.requestShutdown();
    });

    int rc = app.run(0, nullptr);
    killer.join();

    assert(rc == 0);
    assert(app.cleanup_count == 1);
    assert(app.order.size() == 3);
    assert(app.order[0] == "init");
    assert(app.order[1] == "execute");
    assert(app.order[2] == "cleanup");
    resetLogger();
    std::cout << "  [PASS] test_lifecycle_order_and_cleanup_once" << std::endl;
}

// execute 返回非零时 run 透传返回码且 cleanup 仍调用一次
void test_execute_nonzero_return() {
    resetLogger();
    std::string dir = writeTempConfig(R"(
common:
  log:
    schema_version: 1
    level: INFO
    console:
      enabled: false
    async:
      enabled: false
)");

    DaemonApp app;
    app.setConfigRoots({dir});
    app.execute_rc = 7;
    // execute 立即返回（不等 shutdown）
    // 临时改写 execute：用一个不阻塞的子类
    struct FastApp : public hwyz::Application {
        using Application::requestShutdown;
        int cleanup_count = 0;
        int execute_rc;
        std::vector<std::string> roots;
        std::string svc = "testapp";
        std::string getServiceName() const override { return svc; }
        std::vector<std::string> getConfigRoots() const override { return roots; }
        bool initialize() override { return true; }
        int execute() override { return execute_rc; }
        void cleanup() override { ++cleanup_count; }
    } fapp;
    fapp.execute_rc = 7;
    fapp.roots = {dir};

    int rc = fapp.run(0, nullptr);
    assert(rc == 7);
    assert(fapp.cleanup_count == 1);
    resetLogger();
    std::cout << "  [PASS] test_execute_nonzero_return" << std::endl;
}

// run() 输出无裸 std::cout（旧三行成功消息），且含结构化生命周期事件
void test_no_bare_cout_in_run() {
    resetLogger();
    std::string dir = writeTempConfig(R"(
common:
  log:
    schema_version: 1
    level: INFO
    format: standard
    console:
      enabled: true
    async:
      enabled: false
)");

    const char *outFile = "/tmp/tbox_integ_stdout.txt";

    struct App : public hwyz::Application {
        std::string svc = "testapp";
        std::vector<std::string> roots;
        std::string getServiceName() const override { return svc; }
        std::vector<std::string> getConfigRoots() const override { return roots; }
        int execute() override { return 0; }
    } app;
    app.roots = {dir};

    // 捕获 stdout（fd 1）到文件
    int saveOut = dup(STDOUT_FILENO);
    assert(saveOut != -1);
    int fileFd = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fileFd != -1);
    dup2(fileFd, STDOUT_FILENO);
    close(fileFd);

    int rc = app.run(0, nullptr);

    fflush(stdout);
    dup2(saveOut, STDOUT_FILENO);
    close(saveOut);

    assert(rc == 0);

    std::ifstream f(outFile);
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    // 旧三处裸 std::cout 成功消息必须不存在
    assert(content.find("通过 ConfigManager 加载配置成功") == std::string::npos);
    assert(content.find("初始化日志成功") == std::string::npos);
    assert(content.find("设置信号处理成功") == std::string::npos);
    // framework-log standard 格式输出 [ts] [service] [level] message（event 为 common field 不输出）。
    // 验证结构化日志已接管：含 [info] 标记与生命周期 message。
    assert(content.find("[info]") != std::string::npos);
    assert(content.find("application started") != std::string::npos);
    assert(content.find("application stopped") != std::string::npos);

    resetLogger();
    std::cout << "  [PASS] test_no_bare_cout_in_run" << std::endl;
}

int main() {
    std::cout << "Running application integration tests..." << std::endl;
    test_lifecycle_order_and_cleanup_once();
    test_execute_nonzero_return();
    test_no_bare_cout_in_run();
    std::cout << "All application integration tests passed!" << std::endl;
    return 0;
}
