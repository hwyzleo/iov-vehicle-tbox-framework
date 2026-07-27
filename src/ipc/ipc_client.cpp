#include "ipc.h"
#include "ipc_protocol.h"
#include "ipc_log_adapter.h"
#include "log_types.h"

#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>

#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <chrono>
#include <vector>

namespace tbox {
namespace fw {
namespace ipc {

// ============================================================
// Helpers
// ============================================================

static bool connectWithTimeout(int fd, const struct sockaddr* addr,
                               socklen_t addrlen, uint32_t timeout_ms) {
    // Set non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return false;

    int ret = connect(fd, addr, addrlen);
    if (ret == 0) {
        fcntl(fd, F_SETFL, flags);
        return true;
    }
    if (errno != EINPROGRESS) {
        fcntl(fd, F_SETFL, flags);
        return false;
    }

    // Wait for connect to complete
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(fd, &write_fds);

    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(timeout_ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);

    ret = select(fd + 1, nullptr, &write_fds, nullptr, &tv);
    if (ret <= 0) {
        fcntl(fd, F_SETFL, flags);
        return false;  // Timeout or error
    }

    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len);
    fcntl(fd, F_SETFL, flags);
    return error == 0;
}

static uint32_t calculateBackoff(const ReconnectConfig& cfg, uint32_t attempt) {
    uint32_t backoff = cfg.initial_backoff_ms;
    for (uint32_t i = 0; i < attempt && backoff < cfg.max_backoff_ms; i++) {
        backoff = static_cast<uint32_t>(backoff * cfg.multiplier);
        if (backoff > cfg.max_backoff_ms) {
            backoff = cfg.max_backoff_ms;
        }
    }
    return backoff;
}

static std::pair<int32_t, std::string> makeTransportError(IpcError err) {
    return { -static_cast<int32_t>(err), "" };
}

// ============================================================
// Subscription::Impl
// ============================================================
class Subscription::Impl {
public:
    Impl(const std::string& socket_path, const IpcConfig& config,
         uint32_t method_id, uint32_t event_type, EventCallback callback)
        : m_socketPath(socket_path)
        , m_config(config)
        , m_methodId(method_id)
        , m_eventType(event_type)
        , m_callback(std::move(callback)) {
    }

    ~Impl() {
        cancel();
    }

    void start() {
        m_thread = std::thread(&Impl::runLoop, this);
    }

    void cancel() {
        bool expected = true;
        if (!m_active.compare_exchange_strong(expected, false)) {
            return;  // Already cancelled
        }

        int fd = m_fd.exchange(-1);
        if (fd >= 0) {
            shutdown(fd, SHUT_RDWR);
        }

        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    bool isActive() const {
        return m_active.load();
    }

private:
    void runLoop() {
        uint32_t backoff_attempt = 0;

        while (m_active.load()) {
            int fd = doConnectAndSubscribe();

            if (fd < 0) {
                if (!m_active.load()) break;

                // Check if subscription was rejected by server (non-zero status)
                if (fd == -2) {
                    // Server rejected - stop trying
                    m_active.store(false);
                    break;
                }

                // Connection failed - backoff and retry
                uint32_t backoff = calculateBackoff(m_config.reconnect, backoff_attempt);
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
                backoff_attempt++;
                continue;
            }

            // Reset backoff on successful connection
            backoff_attempt = 0;

            // Store fd for cancel()
            m_fd.store(fd);

            // Event-only loop
            eventLoop(fd);

            // Connection broke - close fd and prepare for reconnect
            m_fd.store(-1);
            close(fd);

            if (!m_active.load()) break;

            // Brief backoff before reconnect
            uint32_t backoff = calculateBackoff(m_config.reconnect, backoff_attempt);
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
            backoff_attempt++;
        }

        m_active.store(false);
    }

    // Returns: fd >= 0 on success, -1 on connection failure, -2 on server rejection
    int doConnectAndSubscribe() {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, m_socketPath.c_str(), sizeof(addr.sun_path) - 1);

        if (!connectWithTimeout(fd, reinterpret_cast<struct sockaddr*>(&addr),
                                 sizeof(addr), m_config.connect_timeout_ms)) {
            close(fd);
            return -1;
        }

        // Set SO_RCVTIMEO
        struct timeval tv;
        tv.tv_sec = static_cast<time_t>(m_config.receive_timeout_ms / 1000);
        tv.tv_usec = static_cast<suseconds_t>((m_config.receive_timeout_ms % 1000) * 1000);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Send subscribe Request
        std::string empty_params = "";
        if (!Protocol::writeRequest(fd, m_methodId, empty_params)) {
            close(fd);
            return -1;
        }

        // Read Response (only one frame)
        int32_t status_code = 0;
        std::string response_json;
        if (!Protocol::readResponse(fd, status_code, response_json,
                                     m_config.max_frame_bytes)) {
            close(fd);
            return -1;
        }

        if (status_code != 0) {
            // Server rejected subscription
            close(fd);
            return -2;
        }

        return fd;
    }

    void eventLoop(int fd) {
        while (m_active.load()) {
            uint32_t event_type = 0;
            std::string payload_json;

            if (!Protocol::readEvent(fd, event_type, payload_json,
                                      m_config.max_frame_bytes)) {
                break;  // Read failed - will reconnect
            }

            // Call callback outside any internal lock
            if (m_callback) {
                try {
                    m_callback(event_type, payload_json);
                } catch (...) {
                    // Suppress callback exceptions to keep read thread alive
                }
            }
        }
    }

    std::string m_socketPath;
    IpcConfig m_config;
    uint32_t m_methodId;
    uint32_t m_eventType;
    EventCallback m_callback;

    std::atomic<bool> m_active{true};
    std::atomic<int> m_fd{-1};
    std::thread m_thread;
};

// ============================================================
// Client::Impl
// ============================================================
class Client::Impl {
public:
    Impl(const std::string& socket_path, const IpcConfig& config)
        : m_socketPath(socket_path)
        , m_config(config) {
    }

    ~Impl() {
        disconnect();
    }

    bool connect() {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_connected) {
            return true;
        }
        return doConnect();
    }

    void disconnect() {
        // Cancel all active subscriptions first
        {
            std::lock_guard<std::mutex> lock(m_subMutex);
            for (auto& weak : m_subscriptions) {
                if (auto sp = weak.lock()) {
                    sp->cancel();
                }
            }
            m_subscriptions.clear();
        }

        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_fd >= 0) {
            close(m_fd);
            m_fd = -1;
        }
        m_connected = false;
    }

    bool is_connected() const {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return m_connected;
    }

    std::pair<int32_t, std::string> call(uint32_t method_id,
                                          const std::string& params_json) {
        // Serialize calls on the main connection
        std::lock_guard<std::mutex> call_lock(m_callMutex);

        // Lazy connect
        {
            std::lock_guard<std::mutex> state_lock(m_stateMutex);
            if (!m_connected) {
                if (!doConnect()) {
                    return makeTransportError(IpcError::kSocketFailed);
                }
            }
        }

        // First attempt
        int fd = getFd();
        if (fd < 0) {
            return makeTransportError(IpcError::kSocketFailed);
        }

        if (doCall(fd, method_id, params_json)) {
            return std::move(m_lastResult);
        }

        // First attempt failed - close and reconnect
        {
            std::lock_guard<std::mutex> state_lock(m_stateMutex);
            if (m_fd >= 0) {
                close(m_fd);
                m_fd = -1;
            }
            m_connected = false;
        }

        // Backoff before reconnect
        uint32_t backoff = m_config.reconnect.initial_backoff_ms;
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff));

        // Reconnect
        {
            std::lock_guard<std::mutex> state_lock(m_stateMutex);
            if (!doConnect()) {
                return makeTransportError(IpcError::kSocketFailed);
            }
        }

        // Retry once
        fd = getFd();
        if (fd < 0) {
            return makeTransportError(IpcError::kSocketFailed);
        }

        if (doCall(fd, method_id, params_json)) {
            return std::move(m_lastResult);
        }

        // Second failure - close and return error
        {
            std::lock_guard<std::mutex> state_lock(m_stateMutex);
            if (m_fd >= 0) {
                close(m_fd);
                m_fd = -1;
            }
            m_connected = false;
        }

        return makeTransportError(IpcError::kTransportFailed);
    }

    std::pair<int32_t, std::string> callOnce(uint32_t method_id,
                                             const std::string& params_json) {
        // Serialize calls on the main connection
        std::lock_guard<std::mutex> call_lock(m_callMutex);

        // Lazy connect
        {
            std::lock_guard<std::mutex> state_lock(m_stateMutex);
            if (!m_connected) {
                if (!doConnect()) {
                    return makeTransportError(IpcError::kSocketFailed);
                }
            }
        }

        // Single attempt - no retry
        int fd = getFd();
        if (fd < 0) {
            return makeTransportError(IpcError::kSocketFailed);
        }

        if (doCall(fd, method_id, params_json)) {
            return std::move(m_lastResult);
        }

        // Failure - close and return error (no reconnect, no retry)
        {
            std::lock_guard<std::mutex> state_lock(m_stateMutex);
            if (m_fd >= 0) {
                close(m_fd);
                m_fd = -1;
            }
            m_connected = false;
        }

        return makeTransportError(IpcError::kTransportFailed);
    }

    Subscription subscribe(uint32_t method_id, uint32_t event_type,
                           EventCallback callback) {
        auto impl = std::make_shared<Subscription::Impl>(
            m_socketPath, m_config, method_id, event_type, std::move(callback));
        {
            std::lock_guard<std::mutex> lock(m_subMutex);
            m_subscriptions.push_back(impl);
        }
        impl->start();
        return Subscription(impl);
    }

private:
    bool doConnect() {
        // Caller must hold m_stateMutex
        m_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_fd < 0) {
            return false;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, m_socketPath.c_str(), sizeof(addr.sun_path) - 1);

        if (!connectWithTimeout(m_fd, reinterpret_cast<struct sockaddr*>(&addr),
                                 sizeof(addr), m_config.connect_timeout_ms)) {
            close(m_fd);
            m_fd = -1;
            return false;
        }

        // Set SO_RCVTIMEO
        struct timeval tv;
        tv.tv_sec = static_cast<time_t>(m_config.receive_timeout_ms / 1000);
        tv.tv_usec = static_cast<suseconds_t>((m_config.receive_timeout_ms % 1000) * 1000);
        setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        m_connected = true;
        return true;
    }

    bool doCall(int fd, uint32_t method_id, const std::string& params_json) {
        if (!Protocol::writeRequest(fd, method_id, params_json)) {
            return false;
        }

        int32_t status_code = 0;
        std::string response_json;
        if (!Protocol::readResponse(fd, status_code, response_json,
                                     m_config.max_frame_bytes)) {
            return false;
        }

        m_lastResult = {status_code, std::move(response_json)};
        return true;
    }

    int getFd() {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return m_connected ? m_fd : -1;
    }

    std::string m_socketPath;
    IpcConfig m_config;

    std::mutex m_callMutex;   // Serializes call() on the main connection
    mutable std::mutex m_stateMutex;  // Protects m_fd, m_connected
    int m_fd{-1};
    bool m_connected{false};

    std::mutex m_subMutex;  // Protects m_subscriptions
    std::vector<std::weak_ptr<Subscription::Impl>> m_subscriptions;

    std::pair<int32_t, std::string> m_lastResult;
};

// ============================================================
// Subscription facade
// ============================================================

Subscription::Subscription()
    : m_impl(nullptr) {
}

Subscription::Subscription(std::shared_ptr<Impl> impl)
    : m_impl(std::move(impl)) {
}

Subscription::~Subscription() {
    if (m_impl) {
        m_impl->cancel();
    }
}

Subscription::Subscription(Subscription&& other) noexcept
    : m_impl(std::move(other.m_impl)) {
}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        if (m_impl) {
            m_impl->cancel();
        }
        m_impl = std::move(other.m_impl);
    }
    return *this;
}

void Subscription::cancel() {
    if (m_impl) {
        m_impl->cancel();
    }
}

bool Subscription::isActive() const {
    return m_impl && m_impl->isActive();
}

// ============================================================
// Client facade
// ============================================================

Client::Client(const std::string& socket_path, const IpcConfig& config)
    : m_impl(std::make_unique<Impl>(socket_path, config)) {
}

Client::~Client() = default;

bool Client::connect() {
    return m_impl->connect();
}

void Client::disconnect() {
    m_impl->disconnect();
}

bool Client::is_connected() const {
    return m_impl->is_connected();
}

std::pair<int32_t, std::string> Client::call(
    uint32_t method_id, std::string_view params_json) {
    return m_impl->call(method_id, std::string(params_json));
}

std::pair<int32_t, std::string> Client::callOnce(
    uint32_t method_id, std::string_view params_json) {
    return m_impl->callOnce(method_id, std::string(params_json));
}

Subscription Client::subscribe(uint32_t method_id, uint32_t event_type,
                                EventCallback callback) {
    return m_impl->subscribe(method_id, event_type, std::move(callback));
}

} // namespace ipc
} // namespace fw
} // namespace tbox
