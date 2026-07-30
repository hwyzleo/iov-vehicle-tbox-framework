//
// log_spdlog_adapter.cpp
//
// TBOX-FW-DSN-CR-007: spdlog 兼容 adapter 实现。
//

#include "log_spdlog_adapter.h"
#include "log.h"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/base_sink.h"

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>

namespace tbox {
namespace fw {
namespace log {

namespace {

// spdlog 级别 -> framework-log 级别
LogLevel mapSpdlogLevel(spdlog::level::level_enum l) {
    switch (l) {
        case spdlog::level::trace:
            return LogLevel::kTrace;
        case spdlog::level::debug:
            return LogLevel::kDebug;
        case spdlog::level::info:
            return LogLevel::kInfo;
        case spdlog::level::warn:
            return LogLevel::kWarn;
        case spdlog::level::err:
            return LogLevel::kError;
        case spdlog::level::critical:
        case spdlog::level::off:
            return LogLevel::kFatal;
        default:
            return LogLevel::kInfo;
    }
}

// 自定义 spdlog sink：把 spdlog 已格式化的整行经 Logger::forwardRaw
// 转入 framework-log 的统一 sink。不自行打开 console/file，避免第二套 sink。
template<typename Mutex>
class ForwardingSink : public spdlog::sinks::base_sink<Mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg &msg) override {
        // base_sink 已持锁（mutex_），forwardRaw 内部不再引入递归锁。
        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);
        std::string line = fmt::to_string(formatted);
        // 去掉尾部换行：framework-log 的 sink 自行追加换行。
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        Logger::forwardRaw(line, mapSpdlogLevel(msg.level));
    }

    void flush_() override {
        // framework-log 的 flush 由 Application 生命周期统一驱动；此处无需额外动作。
    }
};

// 持有 adapter 创建的 spdlog logger，便于幂等判断与卸载。
std::shared_ptr<spdlog::logger> &adapterLogger() {
    static std::shared_ptr<spdlog::logger> inst;
    return inst;
}

} // namespace

void SpdlogAdapter::installAsDefault() {
    auto &inst = adapterLogger();
    if (inst) {
        // 幂等：已安装则不再创建第二套 sink。
        return;
    }
    auto sink = std::make_shared<ForwardingSink<std::mutex>>();
    // 仅输出消息正文，避免 spdlog 自带前缀与 framework-log 格式冲突。
    sink->set_pattern("%v");
    auto logger = std::make_shared<spdlog::logger>("fw-legacy", sink);
    // 关闭 spdlog 侧级别过滤，统一交由 framework-log 的 LevelFilter 处理。
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);
    inst = logger;
}

void SpdlogAdapter::uninstall() {
    auto &inst = adapterLogger();
    inst.reset();
}

bool SpdlogAdapter::isInstalled() {
    return adapterLogger() != nullptr;
}

} // namespace log
} // namespace fw
} // namespace tbox
