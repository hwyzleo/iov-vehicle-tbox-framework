//
// Created by hwyz_leo on 2025/8/5.
//

#ifndef IOV_VEHICLE_TBOX_FRAMEWORK_APPLICATION_H
#define IOV_VEHICLE_TBOX_FRAMEWORK_APPLICATION_H

#pragma once

#include "yaml-cpp/yaml.h"
#include "config.h"
#include <string>
#include <vector>

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

        // 获取配置参数（已废弃，请使用 CONFIG_SNAPSHOT）
        // 保留用于子项目向后兼容
        [[deprecated("Use CONFIG_SNAPSHOT instead")]]
        YAML::Node getConfig() const;

        // 获取配置快照（推荐）
        std::shared_ptr<const config::ImmutableConfigView> getConfigSnapshot() const;

        // 服务名称，子项目应 override 返回自己的服务名（如 "tsp", "prov"）
        virtual std::string getServiceName() const;

        // 配置根目录列表（优先级从低到高）
        // 默认：{"/etc/tbox/", "./config/"}
        // 子项目可 override 以添加自定义路径
        virtual std::vector<std::string> getConfigRoots() const;

        // 初始化
        virtual bool initialize();

        // 清理
        virtual void cleanup();

        // 主方法
        virtual int execute() = 0;

    private:
        // 通过 ConfigManager 加载配置
        bool load_config();

        // 初始化日志系统
        void setup_logging();

        // 初始化系统信号处理
        static void setup_signal_handlers();

        // 信号处理回调
        static void signal_handler(int signal);

        // 配置参数（从 ConfigManager 桥接，用于 getConfig() 向后兼容）
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
