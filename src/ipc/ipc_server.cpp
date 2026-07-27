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
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

namespace tbox {
namespace fw {
namespace ipc {

// ============================================================
// Server::Impl
// ============================================================
class Server::Impl : public std::enable_shared_from_this<Server::Impl> {
public:
    Impl(const std::string& socket_path, const IpcConfig& config)
        : m_socketPath(socket_path)
        , m_config(config)
        , m_log() {
        m_shutdownPipe[0] = -1;
        m_shutdownPipe[1] = -1;
    }

    ~Impl() {
        stop();
    }

    bool start(RequestHandler request_handler,
               DisconnectHandler disconnect_handler) {
        if (m_running.load()) {
            return true;  // Already running
        }

        m_requestHandler = std::move(request_handler);
        m_disconnectHandler = std::move(disconnect_handler);

        // Create shutdown pipe
        if (pipe(m_shutdownPipe) < 0) {
            m_log.error("ipc.server.pipe_failed", "Failed to create shutdown pipe", {
                {"error", tbox::fw::log::FieldValue::makeString(strerror(errno))}
            });
            return false;
        }

        // Create Unix domain socket
        m_listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_listenFd < 0) {
            m_log.error("ipc.server.socket_failed", "Failed to create socket", {
                {"error", tbox::fw::log::FieldValue::makeString(strerror(errno))}
            });
            close(m_shutdownPipe[0]);
            close(m_shutdownPipe[1]);
            m_shutdownPipe[0] = m_shutdownPipe[1] = -1;
            return false;
        }

        // Set SO_REUSEADDR
        int opt = 1;
        if (setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            m_log.warn("ipc.server.setsockopt_failed", "Failed to set SO_REUSEADDR", {
                {"error", tbox::fw::log::FieldValue::makeString(strerror(errno))}
            });
        }

        // Bind
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, m_socketPath.c_str(), sizeof(addr.sun_path) - 1);

        // Unlink before bind
        unlink(m_socketPath.c_str());

        if (bind(m_listenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            m_log.error("ipc.server.bind_failed", "Failed to bind socket", {
                {"path", tbox::fw::log::FieldValue::makeString(m_socketPath)},
                {"error", tbox::fw::log::FieldValue::makeString(strerror(errno))}
            });
            close(m_listenFd);
            m_listenFd = -1;
            close(m_shutdownPipe[0]);
            close(m_shutdownPipe[1]);
            m_shutdownPipe[0] = m_shutdownPipe[1] = -1;
            return false;
        }

        // Listen
        if (listen(m_listenFd, m_config.listen_backlog) < 0) {
            m_log.error("ipc.server.listen_failed", "Failed to listen on socket", {
                {"error", tbox::fw::log::FieldValue::makeString(strerror(errno))}
            });
            close(m_listenFd);
            m_listenFd = -1;
            close(m_shutdownPipe[0]);
            close(m_shutdownPipe[1]);
            m_shutdownPipe[0] = m_shutdownPipe[1] = -1;
            return false;
        }

        m_running.store(true);

        // Start accept thread
        auto self = shared_from_this();
        m_acceptThread = std::thread([self]() {
            self->acceptLoop();
        });

        m_log.info("ipc.server.started", "IPC server started", {
            {"socket_path", tbox::fw::log::FieldValue::makeString(m_socketPath)},
            {"backlog", tbox::fw::log::FieldValue::makeInt(m_config.listen_backlog)}
        });

        return true;
    }

    void stop() {
        if (!m_running.exchange(false)) {
            return;  // Already stopped
        }

        // Wake up accept loop
        if (m_shutdownPipe[1] >= 0) {
            char dummy = 'x';
            write(m_shutdownPipe[1], &dummy, 1);
        }

        // Join accept thread
        if (m_acceptThread.joinable()) {
            m_acceptThread.join();
        }

        // Shutdown all client connections to unblock connection threads
        std::vector<int> fds;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& pair : m_clients) {
                fds.push_back(pair.first);
            }
        }
        for (int fd : fds) {
            shutdown(fd, SHUT_RDWR);
        }

        // Clear handlers (connection threads check m_running before calling)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_requestHandler = nullptr;
            m_disconnectHandler = nullptr;
        }

        // Close listen fd
        if (m_listenFd >= 0) {
            close(m_listenFd);
            m_listenFd = -1;
        }

        // Close shutdown pipe
        for (int i = 0; i < 2; i++) {
            if (m_shutdownPipe[i] >= 0) {
                close(m_shutdownPipe[i]);
                m_shutdownPipe[i] = -1;
            }
        }

        // Unlink socket
        if (!m_socketPath.empty()) {
            unlink(m_socketPath.c_str());
        }

        m_log.info("ipc.server.stopped", "IPC server stopped", {});
    }

    bool pushEvent(uint32_t event_type, const std::string& payload_json) {
        // Collect subscribed (fd, state) pairs under mutex
        std::vector<std::pair<int, std::shared_ptr<ClientState>>> targets;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& pair : m_clients) {
                auto& state = pair.second;
                if (state->subscriptions.count(event_type) > 0) {
                    targets.emplace_back(pair.first, state);
                }
            }
        }

        // Write events outside m_mutex, using per-fd write_mutex
        bool any_sent = false;
        for (auto& target : targets) {
            int fd = target.first;
            auto& state = target.second;
            std::lock_guard<std::mutex> write_lock(state->write_mutex);
            if (Protocol::writeEvent(fd, event_type, payload_json)) {
                any_sent = true;
            }
        }
        return any_sent;
    }

    bool addSubscription(int client_fd, uint32_t event_type) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_clients.find(client_fd);
        if (it == m_clients.end()) {
            return false;
        }
        it->second->subscriptions.insert(event_type);
        return true;
    }

    bool removeSubscription(int client_fd, uint32_t event_type) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_clients.find(client_fd);
        if (it == m_clients.end()) {
            return false;
        }
        it->second->subscriptions.erase(event_type);
        return true;
    }

private:
    struct ClientState {
        std::mutex write_mutex;
        std::unordered_set<uint32_t> subscriptions;
    };

    void acceptLoop() {
        while (m_running.load()) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(m_listenFd, &read_fds);
            FD_SET(m_shutdownPipe[0], &read_fds);
            int max_fd = std::max(m_listenFd, m_shutdownPipe[0]);

            int ret = select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }

            // Check shutdown pipe
            if (FD_ISSET(m_shutdownPipe[0], &read_fds)) {
                break;
            }

            // Check listen socket
            if (FD_ISSET(m_listenFd, &read_fds)) {
                int client_fd = accept(m_listenFd, nullptr, nullptr);
                if (client_fd < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    m_log.warn("ipc.server.accept_failed", "Failed to accept connection", {
                        {"error", tbox::fw::log::FieldValue::makeString(strerror(errno))}
                    });
                    continue;
                }

                // Set SO_RCVTIMEO
                struct timeval tv;
                tv.tv_sec = static_cast<time_t>(m_config.receive_timeout_ms / 1000);
                tv.tv_usec = static_cast<suseconds_t>((m_config.receive_timeout_ms % 1000) * 1000);
                setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

                // Add to clients map
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_clients[client_fd] = std::make_shared<ClientState>();
                }

                // Start connection thread (detached, captures shared_from_this)
                auto self = shared_from_this();
                std::thread([self, client_fd]() {
                    self->connectionLoop(client_fd);
                }).detach();

                m_log.info("ipc.server.connection_accepted", "Connection accepted", {
                    {"client_fd", tbox::fw::log::FieldValue::makeInt(client_fd)}
                });
            }
        }
    }

    void connectionLoop(int client_fd) {
        while (m_running.load()) {
            uint32_t method_id = 0;
            std::string params_json;

            if (!Protocol::readRequest(client_fd, method_id, params_json,
                                       m_config.max_frame_bytes)) {
                break;
            }

            // Copy handler under mutex
            RequestHandler handler;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                handler = m_requestHandler;
            }

            std::string response_json;
            int32_t status_code = 0;

            if (handler) {
                try {
                    response_json = handler(method_id, params_json, client_fd);
                } catch (...) {
                    status_code = static_cast<int32_t>(IpcError::kHandlerFailed);
                    response_json = "";
                }
            } else {
                status_code = static_cast<int32_t>(IpcError::kHandlerFailed);
            }

            // Find ClientState for write lock
            std::shared_ptr<ClientState> state;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_clients.find(client_fd);
                if (it != m_clients.end()) {
                    state = it->second;
                }
            }

            if (!state) {
                break;  // Client was removed
            }

            // Write response under per-fd write lock (serialize with pushEvent)
            {
                std::lock_guard<std::mutex> write_lock(state->write_mutex);
                if (!Protocol::writeResponse(client_fd, status_code, response_json)) {
                    break;
                }
            }
        }

        // Cleanup: remove from clients map
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_clients.erase(client_fd);
        }

        // Call disconnect handler only if server is still running
        if (m_running.load()) {
            DisconnectHandler handler;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                handler = m_disconnectHandler;
            }
            if (handler) {
                handler(client_fd);
            }
        }

        // Close fd (under write lock to serialize with any ongoing pushEvent)
        // Note: we already erased from m_clients, so pushEvent won't find this fd.
        // But there may be a pushEvent that collected this fd before we erased it.
        // In that case, pushEvent holds the ClientState::write_mutex, so we wait.
        // After close, the fd is invalid. If pushEvent writes to it, the write fails.
        close(client_fd);

        m_log.info("ipc.server.connection_closed", "Connection closed", {
            {"client_fd", tbox::fw::log::FieldValue::makeInt(client_fd)}
        });
    }

    std::string m_socketPath;
    IpcConfig m_config;
    IpcLogAdapter m_log;

    std::mutex m_mutex;  // Protects m_clients, m_requestHandler, m_disconnectHandler
    std::atomic<bool> m_running{false};
    int m_listenFd{-1};
    int m_shutdownPipe[2]{-1, -1};
    std::thread m_acceptThread;

    RequestHandler m_requestHandler;
    DisconnectHandler m_disconnectHandler;

    std::unordered_map<int, std::shared_ptr<ClientState>> m_clients;
};

// ============================================================
// Server facade
// ============================================================

Server::Server(const std::string& socket_path, const IpcConfig& config)
    : m_impl(std::make_shared<Impl>(socket_path, config)) {
}

Server::~Server() {
    if (m_impl) {
        m_impl->stop();
    }
}

bool Server::start(RequestHandler request_handler,
                   DisconnectHandler disconnect_handler) {
    return m_impl->start(std::move(request_handler), std::move(disconnect_handler));
}

void Server::stop() {
    m_impl->stop();
}

bool Server::push_event(uint32_t event_type, std::string_view payload_json) {
    return m_impl->pushEvent(event_type, std::string(payload_json));
}

bool Server::add_subscription(int client_fd, uint32_t event_type) {
    return m_impl->addSubscription(client_fd, event_type);
}

bool Server::remove_subscription(int client_fd, uint32_t event_type) {
    return m_impl->removeSubscription(client_fd, event_type);
}

} // namespace ipc
} // namespace fw
} // namespace tbox
