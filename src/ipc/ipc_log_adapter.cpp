#include "ipc_log_adapter.h"

namespace tbox {
namespace fw {
namespace ipc {

IpcLogAdapter::IpcLogAdapter()
    : m_logger(tbox::fw::log::Logger::get("ipc")) {
}

IpcLogAdapter::IpcLogAdapter(const std::string& peer_service)
    : m_logger(tbox::fw::log::Logger::get("ipc"))
    , m_peerService(peer_service) {
}

void IpcLogAdapter::info(const std::string& event, const std::string& message,
                          std::initializer_list<tbox::fw::log::Field> fields) {
    m_logger.info(event, message, fields);
}

void IpcLogAdapter::warn(const std::string& event, const std::string& message,
                          std::initializer_list<tbox::fw::log::Field> fields) {
    m_logger.warn(event, message, fields);
}

void IpcLogAdapter::error(const std::string& event, const std::string& message,
                           std::initializer_list<tbox::fw::log::Field> fields) {
    m_logger.error(event, message, fields);
}

void IpcLogAdapter::debug(const std::string& event, const std::string& message,
                           std::initializer_list<tbox::fw::log::Field> fields) {
    m_logger.debug(event, message, fields);
}

} // namespace ipc
} // namespace fw
} // namespace tbox
