//
// Created by hwyz_leo on 2025/8/5.
//
// TBOX-FW-DSN-CR-007: 重构 hwyz::Application 实现。
// - 日志统一接入 framework-log（Logger::init + spdlog 兼容 adapter）
// - 信号集合可定制、async-signal-safe、SA_RESTART/SIGPIPE 忽略
// - 长驻主循环提供 isShutdownRequested/requestShutdown/waitForShutdown
// - 删除启动路径裸 std::cout，改为结构化生命周期事件
//
#include "application.h"

#include "log.h"
#include "log/log_config_adapter.h"
#include "log/log_spdlog_adapter.h"
#include "log/log_emergency_writer.h"

#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <chrono>

namespace hwyz {

    // 信号处理所需的全局实例指针。
    // Application 在进程内为单生命周期对象（run 返回前存活），此指针供 async-signal-safe
    // 的 graceful handler 设置退出标志。
    static Application *g_app_instance = nullptr;

    Application::Application() {
        g_app_instance = this;
    }

    int Application::run(int argc, char *argv[]) {
        try {
            // 1. 加载配置（日志尚未初始化，失败仅输出 stderr）
            if (!load_config()) {
                tbox::fw::log::EmergencyWriter::write("application config load failed, aborting\n");
                return -1;
            }
            // 2. 初始化日志（strict 失败 fail-closed，在 initialize 前终止）
            if (!setup_logging()) {
                return -1;
            }
            // 日志接管后，所有生命周期事件采用统一结构化格式。
            tbox::fw::log::Logger logger = tbox::fw::log::Logger::get("application");
            logger.info("application.config.loaded", "config loaded");
            logger.info("application.logging.initialized", "logging initialized");

            // 3. 注册信号（失败终止启动，避免在未知停机策略下运行）
            if (!setup_signal_handlers()) {
                logger.error("application.signal.install.failed",
                             "signal handler registration failed");
                logger.flush();
                return -1;
            }
            logger.info("application.signals.installed", "signal handlers installed");

            // 4. 业务初始化
            if (!initialize()) {
                logger.error("application.initialize.failed", "initialize returned false");
                logger.flush();
                return -1;
            }
            logger.info("application.started", "application started");

            // 5. 主逻辑（协作式：execute 自行通过 isShutdownRequested/waitForShutdown 响应停机）
            int result = 0;
            try {
                result = execute();
            } catch (const std::exception &e) {
                logger.error("application.execute.exception", e.what());
                result = -1;
            }

            // 6. 清理（正常返回、主动退出与 graceful 停机路径均调用一次）
            cleanup();

            if (shutdown_requested_.load(std::memory_order_acquire)) {
                logger.info("application.shutdown.requested", "shutdown requested");
            }
            logger.info("application.stopped", "application stopped");
            logger.flush();
            return result;
        } catch (const std::exception &e) {
            tbox::fw::log::EmergencyWriter::write(
                    std::string("application fatal: ") + e.what() + "\n");
            return -1;
        }
    }

    // ============================================================
    // 日志
    // ============================================================

    bool Application::useFrameworkLog() const {
        return true;
    }

    tbox::fw::log::LogConfig Application::buildLogConfig() const {
        // 直接从已加载配置解析 LogConfig 字段（保留原始值，包括非法值），
        // 由 setup_logging 经 LogConfigAdapter::validate 统一校验：
        // 这样 strict 模式下非法配置能触发 fail-closed，而非被 adapter 静默降级。
        tbox::fw::log::LogConfig config;  // 构造默认值
        if (!config_["common"] || !config_["common"]["log"]) {
            return config;
        }
        YAML::Node logNode = config_["common"]["log"];
        if (logNode["schema_version"])
            config.schema_version = logNode["schema_version"].as<uint32_t>(1);
        if (logNode["level"])
            config.level = tbox::fw::log::logLevelFromString(logNode["level"].as<std::string>("INFO"));
        if (logNode["strict"])
            config.strict = logNode["strict"].as<bool>(false);
        if (logNode["format"])
            config.format = logNode["format"].as<std::string>("standard");
        if (logNode["async"]) {
            YAML::Node a = logNode["async"];
            if (a["enabled"]) config.async_config.enabled = a["enabled"].as<bool>(true);
            if (a["queue_size"]) config.async_config.queue_size = a["queue_size"].as<uint32_t>(4096);
            if (a["flush_interval_ms"])
                config.async_config.flush_interval_ms = a["flush_interval_ms"].as<uint32_t>(1000);
        }
        if (logNode["console"]) {
            YAML::Node c = logNode["console"];
            if (c["enabled"]) config.console_config.enabled = c["enabled"].as<bool>(true);
        }
        if (logNode["file"]) {
            YAML::Node f = logNode["file"];
            if (f["enabled"]) config.file_config.enabled = f["enabled"].as<bool>(false);
            if (f["root"]) config.file_config.root = f["root"].as<std::string>("/var/log/tbox");
            if (f["max_file_size_mb"]) config.file_config.max_file_size_mb = f["max_file_size_mb"].as<uint32_t>(20);
            if (f["max_files"]) config.file_config.max_files = f["max_files"].as<uint32_t>(5);
            if (f["total_budget_mb"]) config.file_config.total_budget_mb = f["total_budget_mb"].as<uint32_t>(100);
        }
        if (logNode["redact"]) {
            YAML::Node r = logNode["redact"];
            if (r["identifiers"]) config.redact_config.identifiers = r["identifiers"].as<std::string>("mask");
            if (r["raw_payload_max_bytes"])
                config.redact_config.raw_payload_max_bytes = r["raw_payload_max_bytes"].as<uint32_t>(256);
        }
        // 服务级覆盖（<svc>.log.*）
        const std::string svc = getServiceName();
        if (config_[svc] && config_[svc]["log"]) {
            YAML::Node svcLog = config_[svc]["log"];
            if (svcLog["level"])
                config.level = tbox::fw::log::logLevelFromString(svcLog["level"].as<std::string>("INFO"));
            if (svcLog["format"]) config.format = svcLog["format"].as<std::string>("standard");
            if (svcLog["modules"]) {
                YAML::Node mods = svcLog["modules"];
                for (auto it = mods.begin(); it != mods.end(); ++it) {
                    config.module_levels[it->first.as<std::string>()] =
                            tbox::fw::log::logLevelFromString(it->second.as<std::string>("INFO"));
                }
            }
        }
        return config;
    }

    bool Application::setup_logging() {
        if (!useFrameworkLog()) {
            // 短期兼容/测试路径：子类自行管理日志，基类不接入。
            return true;
        }
        tbox::fw::log::LogConfig config = buildLogConfig();

        // 校验配置；非法时按 strict 决策。
        tbox::fw::log::LogErrorInfo verr = tbox::fw::log::LogConfigAdapter::validate(config);
        if (verr.code != tbox::fw::log::LogError::kOk) {
            if (config.strict) {
                fprintf(stderr, "framework-log config invalid (strict): %s\n", verr.message.c_str());
                return false;  // fail-closed
            }
            // 非 strict：降级到默认 console + INFO。
            config = tbox::fw::log::LogConfigAdapter::getDefaultConfig();
        }

        tbox::fw::log::InitResult result;
        try {
            result = tbox::fw::log::Logger::init(getServiceName(), config);
        } catch (const std::exception &e) {
            result.error = tbox::fw::log::LogError::kInitFailed;
            result.error_message = e.what();
        }
        if (result.error != tbox::fw::log::LogError::kOk) {
            if (config.strict) {
                fprintf(stderr, "framework-log init failed (strict): %s\n",
                        result.error_message.c_str());
                return false;  // fail-closed
            }
            // 非 strict：降级重试为最小 stderr logger。
            try {
                tbox::fw::log::Logger::init(getServiceName(),
                                            tbox::fw::log::LogConfigAdapter::getDefaultConfig());
            } catch (...) {
                // 降级也失败：继续运行，日志将走 forwardRaw 的 stderr 兜底。
            }
        }

        // 将兼容 adapter 设为 spdlog 默认 logger，使遗留 spdlog::xxx 进入同一 sink，
        // 不产生第二套 sink 或重复行。服务不得再次 spdlog::set_default_logger()。
        tbox::fw::log::SpdlogAdapter::installAsDefault();
        return true;
    }

    // ============================================================
    // 信号
    // ============================================================

    std::vector<int> Application::gracefulSignals() const {
        return {SIGINT, SIGTERM};
    }

    std::vector<int> Application::fatalSignals() const {
        return {SIGSEGV, SIGABRT};
    }

    std::vector<int> Application::ignoredSignals() const {
        return {SIGPIPE};
    }

    std::string Application::validateSignalSets(const std::vector<int> &graceful,
                                                const std::vector<int> &fatal,
                                                const std::vector<int> &ignored) {
        auto checkSet = [](const std::vector<int> &sigs, const char *name,
                           std::string &err) -> bool {
            std::set<int> seen;
            for (int s : sigs) {
                if (s == SIGKILL || s == SIGSTOP) {
                    err = std::string(name) + " contains SIGKILL/SIGSTOP which cannot be caught";
                    return false;
                }
                if (s < 1) {
                    err = std::string(name) + " contains invalid signal value";
                    return false;
                }
                if (!seen.insert(s).second) {
                    err = std::string(name) + " contains duplicate signal " + std::to_string(s);
                    return false;
                }
            }
            return true;
        };
        std::string err;
        if (!checkSet(graceful, "graceful", err)) return err;
        if (!checkSet(fatal, "fatal", err)) return err;
        if (!checkSet(ignored, "ignored", err)) return err;

        // 跨集合冲突：同一信号出现在多个集合 -> 启动失败。
        std::set<int> all;
        auto checkConflict = [&](const std::vector<int> &sigs, std::string &e) -> bool {
            for (int s : sigs) {
                if (!all.insert(s).second) {
                    e = std::string("signal ") + std::to_string(s) +
                        " appears in multiple sets";
                    return false;
                }
            }
            return true;
        };
        if (!checkConflict(graceful, err)) return err;
        if (!checkConflict(fatal, err)) return err;
        if (!checkConflict(ignored, err)) return err;
        return "";
    }

    bool Application::setup_signal_handlers() {
        auto graceful = gracefulSignals();
        auto fatal = fatalSignals();
        auto ignored = ignoredSignals();

        std::string err = validateSignalSets(graceful, fatal, ignored);
        if (!err.empty()) {
            fprintf(stderr, "signal set validation failed: %s\n", err.c_str());
            return false;
        }

        // graceful：SA_RESTART，被信号中断的系统调用自动恢复。
        for (int s : graceful) {
            struct sigaction sa{};
            sa.sa_handler = graceful_signal_handler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = SA_RESTART;
            if (sigaction(s, &sa, nullptr) == -1) {
                fprintf(stderr, "register graceful signal %d failed\n", s);
                return false;
            }
        }
        // fatal：SA_RESETHAND，处理一次后恢复默认动作，避免 handler 递归/死循环。
        for (int s : fatal) {
            struct sigaction sa{};
            sa.sa_handler = fatal_signal_handler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = SA_RESETHAND;
            if (sigaction(s, &sa, nullptr) == -1) {
                fprintf(stderr, "register fatal signal %d failed\n", s);
                return false;
            }
        }
        // ignored：SIG_IGN。
        for (int s : ignored) {
            struct sigaction sa{};
            sa.sa_handler = SIG_IGN;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            if (sigaction(s, &sa, nullptr) == -1) {
                fprintf(stderr, "register ignored signal %d failed\n", s);
                return false;
            }
        }
        return true;
    }

    // 优雅退出信号处理函数（async-signal-safe）：
    // 仅 write 固定字符串 + 设置无锁原子退出标志。禁止 snprintf/iostream/spdlog/malloc/锁。
    void Application::graceful_signal_handler(int signal) {
        static const char msg[] = "received graceful shutdown signal\n";
        ssize_t n = write(STDERR_FILENO, msg, sizeof(msg) - 1);
        (void) n;
        if (g_app_instance) {
            g_app_instance->shutdown_requested_.store(true, std::memory_order_release);
        }
    }

    // 致命故障信号处理函数：记录后终止，配合 SA_RESETHAND 不返回出错现场。
    void Application::fatal_signal_handler(int signal) {
        static const char msg[] = "fatal signal received, terminating\n";
        ssize_t n = write(STDERR_FILENO, msg, sizeof(msg) - 1);
        (void) n;
        _exit(128 + signal);
    }

    // ============================================================
    // 长驻主循环
    // ============================================================

    bool Application::isShutdownRequested() const noexcept {
        return shutdown_requested_.load(std::memory_order_acquire);
    }

    void Application::requestShutdown() noexcept {
        shutdown_requested_.store(true, std::memory_order_release);
    }

    void Application::waitForShutdown(std::chrono::milliseconds poll) {
        if (poll <= std::chrono::milliseconds(0)) {
            poll = std::chrono::milliseconds(100);
        }
        // sleep-poll，不忙等。信号 handler / 业务线程通过原子置位唤醒。
        while (!shutdown_requested_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(poll);
        }
    }

    // ============================================================
    // 既有契约
    // ============================================================

    YAML::Node Application::getConfig() const {
        return config_;
    }

    std::shared_ptr<const config::ImmutableConfigView> Application::getConfigSnapshot() const {
        return CONFIG_MANAGER.getSnapshot();
    }

    std::string Application::getServiceName() const {
        return "tbox";
    }

    std::vector<std::string> Application::getConfigRoots() const {
        return {"/etc/tbox/", "./config/"};
    }

    bool Application::initialize() {
        return true;
    }

    void Application::cleanup() {
    }

    bool Application::load_config() {
        std::string serviceName = getServiceName();
        std::vector<std::string> configRoots = getConfigRoots();

        // 允许通过环境变量 CONFIG_FILE 指定额外的配置文件（追加到候选列表）
        const char *custom_config = std::getenv("CONFIG_FILE");
        if (custom_config && custom_config[0] != '\0') {
            std::string customPath(custom_config);
            std::string customDir = "./";
            auto lastSlash = customPath.find_last_of('/');
            if (lastSlash != std::string::npos) {
                customDir = customPath.substr(0, lastSlash + 1);
            }
            configRoots.push_back(customDir);
        }

        auto &cm = CONFIG_MANAGER;
        auto err = cm.load(serviceName, configRoots);
        if (err != config::ConfigError::kOk) {
            // 日志尚未初始化，失败仅输出 stderr。
            auto info = cm.getLastError();
            std::cerr << "加载配置文件失败: " << info.message
                      << " (路径: " << info.path << ")" << std::endl;
            std::cerr << "已尝试以下配置根目录:" << std::endl;
            for (const auto &root : configRoots) {
                std::cerr << "  - " << root << std::endl;
            }
            return false;
        }

        // 桥接：从 ConfigManager 获取合并后的 YAML::Node，用于 getConfig() 向后兼容。
        config_ = cm.toYaml();
        // CR-007: 删除裸 std::cout "通过 ConfigManager 加载配置成功"，
        // 改由 run() 在日志初始化后记录 application.config.loaded 结构化事件。
        return true;
    }

}
