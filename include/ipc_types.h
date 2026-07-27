#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <utility>

namespace tbox {
namespace fw {
namespace ipc {

// ============================================================
// Error codes (FW-0301~0307)
// ============================================================
enum class IpcError : uint32_t {
    kOk = 0,
    kSocketFailed = 301,        // FW-0301: socket create/config/connect failed
    kBindListenFailed = 302,    // FW-0302: bind/listen/accept failed
    kFrameInvalid = 303,        // FW-0303: frame header/length invalid or over limit
    kTransportFailed = 304,     // FW-0304: read/write timeout, EOF, transport failure
    kSerializationFailed = 305, // FW-0305: JSON/base64 serialization failed
    kHandlerFailed = 306,       // FW-0306: unknown method or request handler failed
    kSubscriptionFailed = 307   // FW-0307: subscription register/push/recover failed
};

// ============================================================
// Error info
// ============================================================
struct IpcErrorInfo {
    IpcError code;
    std::string message;
    std::string detail;

    IpcErrorInfo() : code(IpcError::kOk) {}
    IpcErrorInfo(IpcError c, const std::string& m, const std::string& d = "")
        : code(c), message(m), detail(d) {}
};

// ============================================================
// Exception
// ============================================================
class IpcException : public std::exception {
public:
    IpcException(IpcError code, const std::string& message, const std::string& detail = "")
        : m_error{code, message, detail} {}

    IpcErrorInfo getError() const { return m_error; }
    const char* what() const noexcept override { return m_error.message.c_str(); }

private:
    IpcErrorInfo m_error;
};

// ============================================================
// Reconnect config
// ============================================================
struct ReconnectConfig {
    uint32_t initial_backoff_ms = 100;
    uint32_t max_backoff_ms = 5000;
    double multiplier = 2.0;
};

// ============================================================
// IPC config (injected by calling service, not directly dependent
// on framework-config)
// ============================================================
struct IpcConfig {
    uint32_t max_frame_bytes = 10485760;  // 10 MiB
    uint32_t receive_timeout_ms = 60000;
    uint32_t connect_timeout_ms = 3000;
    int listen_backlog = 5;
    ReconnectConfig reconnect;
};

// ============================================================
// Packed wire headers (native byte order, no version byte,
// no unified frame type field)
// ============================================================
#pragma pack(push, 1)
struct RequestHeader {
    uint32_t method_id;
    uint32_t params_length;
};

struct ResponseHeader {
    int32_t  status_code;
    uint32_t data_length;
};

struct EventHeader {
    uint32_t event_type;
    uint32_t payload_length;
};
#pragma pack(pop)

static_assert(sizeof(RequestHeader) == 8, "RequestHeader must be 8 bytes");
static_assert(sizeof(ResponseHeader) == 8, "ResponseHeader must be 8 bytes");
static_assert(sizeof(EventHeader) == 8, "EventHeader must be 8 bytes");

// ============================================================
// Default config factory
// ============================================================
inline IpcConfig getDefaultIpcConfig() {
    return IpcConfig{};
}

// ============================================================
// Type aliases
// ============================================================
using RequestHandler = std::function<std::string(
    uint32_t method_id, std::string_view params_json, int client_fd)>;
using DisconnectHandler = std::function<void(int client_fd)>;
using EventCallback = std::function<void(
    uint32_t event_type, std::string_view payload_json)>;

} // namespace ipc
} // namespace fw
} // namespace tbox
