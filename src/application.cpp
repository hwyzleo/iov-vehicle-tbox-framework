//
// Created by hwyz_leo on 2025/8/5.
//
#include <iostream>
#include <csignal>
#include <cstdlib>
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

    void Application::setup_signal_handlers() {
        struct sigaction sa{};
        sa.sa_handler = signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        if (sigaction(SIGABRT, &sa, nullptr) == -1) {
            std::cerr << "注册SIGABRT信号失败" << std::endl;
        }
        // Ctrl+C
        if (sigaction(SIGINT, &sa, nullptr) == -1) {
            std::cerr << "注册SIGINT信号失败" << std::endl;
        }
        // 终止信号
        if (sigaction(SIGTERM, &sa, nullptr) == -1) {
            std::cerr << "注册SIGTERM信号失败" << std::endl;
        }
        // 段错误
        if (sigaction(SIGSEGV, &sa, nullptr) == -1) {
            std::cerr << "注册SIGSEGV信号失败" << std::endl;
        }
        std::cout << "设置信号处理成功" << std::endl;
    }

    void Application::signal_handler(int signal) {
        char msg[100];
        int len = snprintf(msg, sizeof(msg), "收到信号: %d\n", signal);
        write(STDOUT_FILENO, msg, len);
        if (g_app_instance) {
            g_app_instance->shutdown_requested_ = true;
        }
    }

}