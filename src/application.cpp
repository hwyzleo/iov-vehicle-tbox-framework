//
// Created by hwyz_leo on 2025/8/5.
//
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "../third_party/include/spdlog/spdlog.h"
#include "../third_party/include/spdlog/sinks/stdout_color_sinks.h"
#include "../third_party/include/spdlog/sinks/basic_file_sink.h"

#include "application.h"

namespace hwyz {

    // 退出请求
    volatile sig_atomic_t Application::shutdown_requested_ = 0;

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
            // 执行用户初始化
            if (!initialize()) {
                return -1;
            }
            // 执行主逻辑
            int result = execute();
            while (!shutdown_requested_) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            // 清理资源
            cleanup();
            return result;
        } catch (const std::exception &e) {
            std::cerr << "应用启动失败: " << e.what() << std::endl;
            return -1;
        }
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
    }

    void Application::setup_signal_handlers() {
        signal(SIGINT, signal_handler);   // Ctrl+C
        signal(SIGTERM, signal_handler);  // 终止信号
        signal(SIGSEGV, signal_handler);  // 段错误
    }

    void Application::signal_handler(int signal) {
        if (g_app_instance) {
            std::cout << "收到系统信号: " << signal << std::endl;
            shutdown_requested_ = 1;
        }
    }

}