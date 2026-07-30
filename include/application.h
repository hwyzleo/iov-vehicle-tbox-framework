//
// Created by hwyz_leo on 2025/8/5.
//
// TBOX-FW-DSN-CR-007: 重构 hwyz::Application 基类。
// 统一接入 framework-log；提供可定制且 async-signal-safe 的信号处理；
// 为长驻 execute() 提供退出查询/请求/等待能力；保留 getConfig() deprecated。
//

#ifndef IOV_VEHICLE_TBOX_FRAMEWORK_APPLICATION_H
#define IOV_VEHICLE_TBOX_FRAMEWORK_APPLICATION_H

#pragma once

#include "yaml-cpp/yaml.h"
#include "config.h"
#include "log_types.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <string>
#include <vector>

namespace hwyz {

    class Application {
    public:
        virtual ~Application() = default;

        // 禁止拷贝构造和赋值
        Application(const Application &) = delete;

        Application &operator=(const Application &) = delete;

        // 启动应用主生命周期：
        //   配置加载 -> 日志初始化 -> 信号注册 -> initialize() -> execute() -> cleanup() -> flush
        int run(int argc, char *argv[]);

        // 校验三类信号集合（去重、跨集合冲突、拒 SIGKILL/SIGSTOP/非法值）。
        // 返回空串表示通过，否则返回失败原因。无副作用，供子类预校验与单测直接验证。
        static std::string validateSignalSets(const std::vector<int> &graceful,
                                              const std::vector<int> &fatal,
                                              const std::vector<int> &ignored);

    protected:
        Application();

        // ============ 日志 ============
        // 默认通过 framework-log 初始化结构化日志（返回 true）。
        // 仅用于短期兼容或测试时覆盖为 false；生产服务不得以此恢复第二套日志 schema。
        virtual bool useFrameworkLog() const;

        // 从已加载配置构造 framework-log 的 LogConfig，沿用其校验与默认值。
        virtual tbox::fw::log::LogConfig buildLogConfig() const;

        // ============ 信号 ============
        // 子类可覆盖信号集合，但不覆盖安装机制。三类集合须互斥且去重；
        // SIGKILL/SIGSTOP 及非法值会导致启动失败。
        virtual std::vector<int> gracefulSignals() const;  // 默认 {SIGINT, SIGTERM}
        virtual std::vector<int> fatalSignals() const;     // 默认 {SIGSEGV, SIGABRT}
        virtual std::vector<int> ignoredSignals() const;   // 默认 {SIGPIPE}

        // ============ 长驻主循环 ============
        // 查询框架是否收到停机请求（noexcept，跨线程无数据竞争）。
        bool isShutdownRequested() const noexcept;

        // 主动请求停机（noexcept，幂等）。可在业务线程调用。
        void requestShutdown() noexcept;

        // 阻塞等待停机请求，按给定周期轮询；poll <= 0 时使用默认 100ms。不忙等。
        void waitForShutdown(
                std::chrono::milliseconds poll = std::chrono::milliseconds{100});

        // ============ 既有生命周期契约（保持不变） ============
        virtual bool initialize();

        virtual void cleanup();

        virtual int execute() = 0;

        // 服务名称，子项目应 override 返回自己的服务名（如 "tsp", "prov"）
        virtual std::string getServiceName() const;

        // 配置根目录列表（优先级从低到高）
        // 默认：{"/etc/tbox/", "./config/"}
        virtual std::vector<std::string> getConfigRoots() const;

        // 获取配置快照（推荐，framework-config 类型化访问）
        std::shared_ptr<const config::ImmutableConfigView> getConfigSnapshot() const;

        // 获取配置参数（已废弃，请使用 CONFIG_SNAPSHOT）
        // 本期保留用于子项目向后兼容；阶段 2/3 迁移清零调用后另立 breaking CR 删除。
        [[deprecated("Use CONFIG_SNAPSHOT instead")]]
        YAML::Node getConfig() const;

        // ============ 生命周期内部步骤（protected，便于子类复用与测试） ============
        // 通过 ConfigManager 加载配置；成功返回 true。
        bool load_config();

        // 初始化日志系统（framework-log + spdlog 兼容 adapter）；
        // strict 模式初始化失败返回 false（fail-closed），非 strict 降级到 console+INFO。
        bool setup_logging();

        // 注册 ignored / graceful / fatal 信号；任一注册失败返回 false（终止启动）。
        bool setup_signal_handlers();

    private:
        // 信号处理回调（async-signal-safe）
        static void graceful_signal_handler(int signal);

        static void fatal_signal_handler(int signal);

        // 配置参数（从 ConfigManager 桥接，用于 getConfig() 向后兼容）
        YAML::Node config_;

        // 退出请求标志：lock-free 原子，满足 async-signal-safe 与跨线程无数据竞争。
        std::atomic<bool> shutdown_requested_{false};
    };

} // namespace hwyz

// 编译期验证原子标志无锁，否则信号上下文写入不安全（CR AD5）。
static_assert(std::atomic<bool>::is_always_lock_free,
              "std::atomic<bool> must be lock-free for async-signal-safe shutdown flag");

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
