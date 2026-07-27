#pragma once

#include "log.h"
#include <string>

namespace tbox {
namespace fw {
namespace ipc {

// ============================================================
// IpcLogAdapter - framework-log 适配器
// 封装 Logger facade，模块名固定为 "ipc"。
// 确保不记录原始 JSON/base64 payload、token、证书等敏感信息。
// ============================================================
class IpcLogAdapter {
public:
    IpcLogAdapter();
    explicit IpcLogAdapter(const std::string& peer_service);

    void info(const std::string& event, const std::string& message,
              std::initializer_list<tbox::fw::log::Field> fields = {});
    void warn(const std::string& event, const std::string& message,
              std::initializer_list<tbox::fw::log::Field> fields = {});
    void error(const std::string& event, const std::string& message,
               std::initializer_list<tbox::fw::log::Field> fields = {});
    void debug(const std::string& event, const std::string& message,
               std::initializer_list<tbox::fw::log::Field> fields = {});

    const std::string& peerService() const { return m_peerService; }
    void setPeerService(const std::string& s) { m_peerService = s; }

private:
    tbox::fw::log::Logger m_logger;
    std::string m_peerService;
};

} // namespace ipc
} // namespace fw
} // namespace tbox
