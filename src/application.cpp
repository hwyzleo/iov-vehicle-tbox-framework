//
// Created by hwyz_leo on 2025/8/5.
//
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <future>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"

#include "application.h"

namespace hwyz {

    // 信号处理函数指针
    static Application *g_app_instance = nullptr;

    Application::Application() {
        g_app_instance = this;
    }

    int Application::run(int argc, char *argv[]) {
        try {
            // 加载配置文件
            if (!load_default_config()) {
                std::cerr << "加载配置文件失败" << std::endl;
                return -1;
            }
            // 初始化日志系统
            setup_logging();
            // 设置信号处理
            setup_signal_handlers();
            // 执行初始化
            if (!initialize()) {
                std::cerr << "初始化失败" << std::endl;
                return -1;
            }
            // 执行主逻辑
            auto future = std::async(std::launch::async, [this]() {
                try {
                    int result = this->execute();
                    spdlog::info("主逻辑执行完成，返回值[{}]", result);
                    return result;
                } catch (const std::exception &e) {
                    spdlog::error("主逻辑发生异常[{}]", e.what());
                    return -1;
                }
            });
            // 检查退出场景
            bool is_check = false;
            while (!shutdown_requested_) {
                if (!is_check && future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
                    int result = future.get();
                    is_check = true;
                    if (result != 0) {
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            // 清理资源
            cleanup();
            return 0;
        } catch (const std::exception &e) {
            std::cerr << "应用启动失败: " << e.what() << std::endl;
            return -1;
        }
    }

    YAML::Node Application::getConfig() const {
        return config_;
    }

    bool Application::initialize() {
        return true;
    }

    void Application::cleanup() {
    }

    bool Application::load_default_config() {
        const char *env = std::getenv("ENV");
        std::string env_str = env ? env : "dev";
        std::string file_path = "../config/config." + env_str + ".yaml";
        std::ifstream file(file_path);
        if (!file.is_open()) {
            return false;
        }
        config_ = YAML::LoadFile(file_path);
        std::cout << "加载配置文件[" + file_path + "]成功" << std::endl;
        return true;
    }

    void Application::setup_logging() {
        std::string logger_type = config_["logger"]["type"].as<std::string>();
        if (logger_type == "file") {
            std::string logger_path = config_["logger"]["path"].as<std::string>();
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logger_path, true);
            file_sink->set_level(spdlog::level::debug);
            auto logger = std::make_shared<spdlog::logger>("file_logger", file_sink);
            logger->set_level(spdlog::level::debug);
            spdlog::set_default_logger(logger);
            spdlog::flush_every(std::chrono::seconds(5));
        } else {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(spdlog::level::debug);
            auto logger = std::make_shared<spdlog::logger>("console", console_sink);
            logger->set_level(spdlog::level::debug);
            spdlog::set_default_logger(logger);
        }
        std::cout << "初始化日志成功" << std::endl;
    }

    // 致命的同步故障信号（段错误 / abort）处理函数。
    // 这类信号是由出错指令同步触发的：如果 handler 只打印然后 return，
    // 内核会回到出错指令重新执行，立即再次触发同一信号，形成死循环刷屏。
    // 因此这里记录后必须终止进程，绝不能返回到出错现场。
    static void fatal_signal_handler(int signal) {
        char msg[64];
        int len = snprintf(msg, sizeof(msg), "收到致命信号: %d，进程即将退出\n", signal);
        write(STDERR_FILENO, msg, len);
        // 直接终止进程（异步信号安全）。配合 SA_RESETHAND 双重保险，
        // 即使这里未退出，信号处理也已恢复为默认动作，不会再回到本 handler。
        _exit(128 + signal);
    }

    void Application::setup_signal_handlers() {
        // 优雅退出信号（SIGINT / SIGTERM）：设置退出标志，交由主循环收尾。
        struct sigaction sa_graceful{};
        sa_graceful.sa_handler = signal_handler;
        sigemptyset(&sa_graceful.sa_mask);
        sa_graceful.sa_flags = 0;

        // Ctrl+C
        if (sigaction(SIGINT, &sa_graceful, nullptr) == -1) {
            std::cerr << "注册SIGINT信号失败" << std::endl;
        }
        // 终止信号
        if (sigaction(SIGTERM, &sa_graceful, nullptr) == -1) {
            std::cerr << "注册SIGTERM信号失败" << std::endl;
        }

        // 致命故障信号（SIGSEGV / SIGABRT）：记录后终止，禁止返回式处理。
        // SA_RESETHAND：处理一次后自动恢复为默认动作，避免任何形式的死循环。
        struct sigaction sa_fatal{};
        sa_fatal.sa_handler = fatal_signal_handler;
        sigemptyset(&sa_fatal.sa_mask);
        sa_fatal.sa_flags = SA_RESETHAND;

        // 段错误
        if (sigaction(SIGSEGV, &sa_fatal, nullptr) == -1) {
            std::cerr << "注册SIGSEGV信号失败" << std::endl;
        }
        // abort
        if (sigaction(SIGABRT, &sa_fatal, nullptr) == -1) {
            std::cerr << "注册SIGABRT信号失败" << std::endl;
        }
        std::cout << "设置信号处理成功" << std::endl;
    }

    // 优雅退出信号处理函数：仅设置退出标志，主循环发现后正常收尾。
    void Application::signal_handler(int signal) {
        char msg[64];
        int len = snprintf(msg, sizeof(msg), "收到退出信号: %d\n", signal);
        write(STDOUT_FILENO, msg, len);
        if (g_app_instance) {
            g_app_instance->shutdown_requested_ = true;
        }
    }

}