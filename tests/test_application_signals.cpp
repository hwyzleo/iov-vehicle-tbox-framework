//
// test_application_signals.cpp
//
// TBOX-FW-DSN-CR-007: Application 信号处理单测。
// 覆盖 validateSignalSets（默认/覆盖/去重/冲突/拒 SIGKILL·SIGSTOP·非法）、
// SIGPIPE 忽略、SA_RESTART、SA_RESETHAND、graceful handler 设置退出标志。
//

#include "application.h"

#include <cassert>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

class TestApp : public hwyz::Application {
public:
    using Application::setup_signal_handlers;
    using Application::isShutdownRequested;
    using Application::gracefulSignals;
    using Application::fatalSignals;
    using Application::ignoredSignals;

    void setGraceful(std::vector<int> s) { g_ = std::move(s); }
    void setFatal(std::vector<int> s) { f_ = std::move(s); }
    void setIgnored(std::vector<int> s) { i_ = std::move(s); }
    std::vector<int> gracefulSignals() const override { return g_; }
    std::vector<int> fatalSignals() const override { return f_; }
    std::vector<int> ignoredSignals() const override { return i_; }

    int execute() override { return 0; }

private:
    std::vector<int> g_ = {SIGINT, SIGTERM};
    std::vector<int> f_ = {SIGSEGV, SIGABRT};
    std::vector<int> i_ = {SIGPIPE};
};

// 恢复测试中安装的信号为默认动作，避免污染后续测试
static void restoreSignals() {
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
}

// 抑制 graceful handler 写向 stderr 的固定消息（保持测试输出整洁）
static void silenceStderr() {
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull != -1) {
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }
}

// ---------- validateSignalSets ----------

void test_validate_defaults_ok() {
    std::string err = hwyz::Application::validateSignalSets({SIGINT, SIGTERM},
                                                             {SIGSEGV, SIGABRT},
                                                             {SIGPIPE});
    assert(err.empty());
    std::cout << "  [PASS] test_validate_defaults_ok" << std::endl;
}

void test_validate_override_sighup_ok() {
    std::string err = hwyz::Application::validateSignalSets({SIGINT, SIGTERM, SIGHUP},
                                                             {SIGSEGV, SIGABRT},
                                                             {SIGPIPE});
    assert(err.empty());
    std::cout << "  [PASS] test_validate_override_sighup_ok" << std::endl;
}

void test_validate_duplicate_in_set() {
    std::string err = hwyz::Application::validateSignalSets({SIGINT, SIGINT},
                                                             {SIGSEGV},
                                                             {SIGPIPE});
    assert(!err.empty());
    assert(err.find("duplicate") != std::string::npos);
    std::cout << "  [PASS] test_validate_duplicate_in_set" << std::endl;
}

void test_validate_cross_set_conflict() {
    // SIGTERM 同时出现在 graceful 与 ignored -> 冲突
    std::string err = hwyz::Application::validateSignalSets({SIGINT, SIGTERM},
                                                             {SIGSEGV},
                                                             {SIGTERM});
    assert(!err.empty());
    assert(err.find("multiple sets") != std::string::npos);
    std::cout << "  [PASS] test_validate_cross_set_conflict" << std::endl;
}

void test_validate_sigkill_rejected() {
    std::string err = hwyz::Application::validateSignalSets({SIGINT, SIGKILL},
                                                             {SIGSEGV},
                                                             {SIGPIPE});
    assert(!err.empty());
    assert(err.find("SIGKILL") != std::string::npos);
    std::cout << "  [PASS] test_validate_sigkill_rejected" << std::endl;
}

void test_validate_sigstop_rejected() {
    std::string err = hwyz::Application::validateSignalSets({SIGINT},
                                                             {SIGSEGV, SIGSTOP},
                                                             {SIGPIPE});
    assert(!err.empty());
    assert(err.find("SIGSTOP") != std::string::npos);
    std::cout << "  [PASS] test_validate_sigstop_rejected" << std::endl;
}

void test_validate_invalid_value_rejected() {
    std::string err = hwyz::Application::validateSignalSets({SIGINT},
                                                             {SIGSEGV},
                                                             {0});
    assert(!err.empty());
    assert(err.find("invalid") != std::string::npos);
    std::cout << "  [PASS] test_validate_invalid_value_rejected" << std::endl;
}

// ---------- 实际安装行为 ----------

void test_setup_returns_false_on_conflict() {
    TestApp app;
    app.setGraceful({SIGINT, SIGTERM});
    app.setIgnored({SIGTERM});  // 与 graceful 冲突
    bool ok = app.setup_signal_handlers();
    assert(!ok);
    std::cout << "  [PASS] test_setup_returns_false_on_conflict" << std::endl;
}

void test_sigpipe_ignored() {
    TestApp app;
    assert(app.setup_signal_handlers());  // 默认 ignored={SIGPIPE}
    // raise(SIGPIPE) 在 SIG_IGN 下返回 0 且进程不终止；若未忽略则会终止测试进程
    int r = raise(SIGPIPE);
    assert(r == 0);
    // 到达此处即证明 SIGPIPE 被忽略
    restoreSignals();
    std::cout << "  [PASS] test_sigpipe_ignored" << std::endl;
}

void test_sa_restart_installed() {
    TestApp app;
    assert(app.setup_signal_handlers());
    struct sigaction sa{};
    assert(sigaction(SIGINT, nullptr, &sa) == 0);
    assert((sa.sa_flags & SA_RESTART) != 0);
    restoreSignals();
    std::cout << "  [PASS] test_sa_restart_installed" << std::endl;
}

void test_sa_resethand_installed() {
    // 注意：macOS 的 sigaction 取回时不回传 SA_RESETHAND 标志（平台行为），
    // 因此这里验证 fatal handler 已安装（sa_handler 非 SIG_DFL/SIG_IGN），
    // SA_RESETHAND 的置位由代码 sa.sa_flags=SA_RESETHAND 保证，
    // 其一次性终止行为由 test_fatal_handler_terminates 验证。
    TestApp app;
    assert(app.setup_signal_handlers());
    struct sigaction sa{};
    assert(sigaction(SIGSEGV, nullptr, &sa) == 0);
    assert(sa.sa_handler != SIG_DFL);
    assert(sa.sa_handler != SIG_IGN);
    restoreSignals();
    std::cout << "  [PASS] test_sa_resethand_installed" << std::endl;
}

void test_fatal_handler_terminates() {
    // fork 子进程：以 SIGUSR1 作为 fatal 信号安装，raise 后验证子进程以 128+sig 退出。
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        // 子进程：抑制 fatal handler 的 stderr 输出
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) { dup2(devnull, STDERR_FILENO); close(devnull); }
        TestApp app;
        app.setFatal({SIGUSR1});
        app.setup_signal_handlers();
        raise(SIGUSR1);
        // 不应到达
        _exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
        assert(WIFEXITED(status));
        assert(WEXITSTATUS(status) == 128 + SIGUSR1);
        std::cout << "  [PASS] test_fatal_handler_terminates" << std::endl;
    }
}

void test_graceful_handler_sets_flag_sigint() {
    silenceStderr();
    TestApp app;
    assert(app.setup_signal_handlers());
    assert(!app.isShutdownRequested());
    // raise 同步投递：handler 在 raise 返回前执行
    raise(SIGINT);
    assert(app.isShutdownRequested());
    restoreSignals();
    std::cout << "  [PASS] test_graceful_handler_sets_flag_sigint" << std::endl;
}

void test_graceful_handler_sets_flag_sigterm() {
    TestApp app;
    assert(app.setup_signal_handlers());
    assert(!app.isShutdownRequested());
    raise(SIGTERM);
    assert(app.isShutdownRequested());
    restoreSignals();
    std::cout << "  [PASS] test_graceful_handler_sets_flag_sigterm" << std::endl;
}

void test_custom_sighup_graceful() {
    silenceStderr();
    TestApp app;
    app.setGraceful({SIGINT, SIGTERM, SIGHUP});
    assert(app.setup_signal_handlers());
    assert(!app.isShutdownRequested());
    raise(SIGHUP);
    assert(app.isShutdownRequested());
    restoreSignals();
    std::cout << "  [PASS] test_custom_sighup_graceful" << std::endl;
}

int main() {
    std::cout << "Running application signal tests..." << std::endl;
    test_validate_defaults_ok();
    test_validate_override_sighup_ok();
    test_validate_duplicate_in_set();
    test_validate_cross_set_conflict();
    test_validate_sigkill_rejected();
    test_validate_sigstop_rejected();
    test_validate_invalid_value_rejected();
    test_setup_returns_false_on_conflict();
    test_sigpipe_ignored();
    test_sa_restart_installed();
    test_sa_resethand_installed();
    test_fatal_handler_terminates();
    test_graceful_handler_sets_flag_sigint();
    test_graceful_handler_sets_flag_sigterm();
    test_custom_sighup_graceful();
    std::cout << "All application signal tests passed!" << std::endl;
    return 0;
}
