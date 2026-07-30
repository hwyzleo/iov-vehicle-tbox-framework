//
// log_spdlog_adapter.h
//
// TBOX-FW-DSN-CR-007: framework-log 的 spdlog 兼容 adapter。
// 将 framework-log 的统一 sink 包装为一个 spdlog sink，并设为 spdlog 默认 logger，
// 使遗留 spdlog::info/warn/... 调用进入 framework-log 的同一 sink，
// 不产生第二套 console/file sink，也不产生重复行。
//

#pragma once

#include "log_types.h"

namespace tbox {
namespace fw {
namespace log {

class SpdlogAdapter {
public:
    // 将 framework-log 的 sink 包装为 spdlog sink，并设为 spdlog 默认 logger。
    // 调用后遗留 spdlog::xxx 调用会经 forwardRaw 进入 framework-log 的统一 sink。
    // 幂等：重复调用不会创建第二套 sink 或覆盖既有默认 logger。
    // 前置条件：已调用 Logger::init()（否则转发降级到 stderr）。
    static void installAsDefault();

    // 卸载 adapter（主要用于测试与进程退出清理）。
    static void uninstall();

    // 是否已安装为 spdlog 默认 logger。
    static bool isInstalled();
};

} // namespace log
} // namespace fw
} // namespace tbox
