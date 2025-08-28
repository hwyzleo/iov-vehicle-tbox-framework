//
// Created by hwyz_leo on 2025/8/5.
//

#ifndef IOV_VEHICLE_TBOX_FRAMEWORK_APPLICATION_H
#define IOV_VEHICLE_TBOX_FRAMEWORK_APPLICATION_H

#pragma once

#include "yaml-cpp/yaml.h"

namespace hwyz {

    class Application {
    public:
        virtual ~Application() = default;

        // 禁止拷贝构造和赋值
        Application(const Application &) = delete;

        Application &operator=(const Application &) = delete;

        // 启动应用的主函数
        int run(int argc, char *argv[]);

    protected:
        Application();

        // 获取配置参数
        YAML::Node getConfig() const;

        // 初始化
        virtual bool initialize();

        // 清理
        virtual void cleanup();

        // 主方法
        virtual int execute() = 0;

    private:
        // 加载默认配置文件
        bool load_default_config();

        // 初始化日志系统
        void setup_logging();

        // 初始化系统信号处理
        static void setup_signal_handlers();

        // 信号处理回调
        static void signal_handler(int signal);

        // 配置参数
        YAML::Node config_;

        // 退出请求
        std::atomic<bool> shutdown_requested_{false};

    };

}

#define APPLICATION_ENTRY(AppClass) \
    \
    extern "C" int main(int argc, char* argv[]) { \
        try { \
            AppClass app; \
            return app.run(argc, argv); \
        } catch (const std::exception& e) { \
            fprintf(stderr, "Application terminated with exception: %s\n", e.what()); \
            return -1; \
        } catch (...) { \
            fprintf(stderr, "Application terminated with unknown exception\n"); \
            return -1; \
        } \
    }

#endif //IOV_VEHICLE_TBOX_FRAMEWORK_APPLICATION_H