#pragma once

#include "log_types.h"
#include <string>
#include <memory>
#include <initializer_list>

namespace tbox {
namespace fw {
namespace log {

class LoggerRegistry;

// ============================================================
// Logger facade — 对外统一日志 API
// ============================================================
class Logger {
public:
    // 初始化日志系统（每个服务启动时调用一次）
    static InitResult init(const std::string& service, const LogConfig& config);

    // 获取指定模块的 Logger 实例
    static Logger get(const std::string& module);

    // 关闭日志系统（停止异步线程、释放资源），重置为未初始化状态。
    // 用于进程退出前的优雅清理与测试间重置；调用后可重新 init。
    static void shutdown();

    // 将已格式化的整行直接转发到 framework-log 的统一 sink（不经 enricher/redactor）。
    // 供 spdlog 兼容 adapter 等桥接层使用，使遗留日志进入同一 sink，避免产生第二套 sink。
    // 未初始化时降级写入 stderr。
    static void forwardRaw(const std::string& line, LogLevel level);

    // 日志输出方法
    void trace(std::string_view event, std::string_view message,
               std::initializer_list<Field> fields = {});
    void debug(std::string_view event, std::string_view message,
               std::initializer_list<Field> fields = {});
    void info(std::string_view event, std::string_view message,
              std::initializer_list<Field> fields = {});
    void warn(std::string_view event, std::string_view message,
              std::initializer_list<Field> fields = {});
    void error(std::string_view event, std::string_view message,
               std::initializer_list<Field> fields = {});
    [[noreturn]] void fatal(std::string_view event, std::string_view message,
                            std::initializer_list<Field> fields = {});

    void flush();

private:
    Logger();
    class Impl;
    std::shared_ptr<Impl> m_impl;

    friend class LoggerRegistry;
};

// ============================================================
// ContextScope — RAII 上下文传播
// ============================================================
class ContextScope {
public:
    explicit ContextScope(LogContext context);
    ~ContextScope();

    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;

    static const LogContext* current();

private:
    LogContext m_context;
    const LogContext* m_previous;
};

} // namespace log
} // namespace fw
} // namespace tbox
