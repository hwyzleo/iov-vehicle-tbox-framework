#pragma once

#include "ipc_types.h"
#include <string>
#include <memory>
#include <utility>

namespace tbox {
namespace fw {
namespace ipc {

// ============================================================
// Server -通用 AF_UNIX 服务端 facade
// ============================================================
class Server {
public:
    Server(const std::string& socket_path, const IpcConfig& config = IpcConfig{});
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

    // 创建 socket、bind、listen 并启动 accept loop。
    // request_handler:  按 method_id 分发请求，返回 response_json。
    // disconnect_handler: 连接断开时回调（最多一次），可为空。
    bool start(RequestHandler request_handler,
               DisconnectHandler disconnect_handler = {});

    // 幂等停机：停止 accept、关闭活动连接、清理订阅、unlink socket。
    void stop();

    // 向已订阅 event_type 的连接推送 Event 帧。
    bool push_event(uint32_t event_type, std::string_view payload_json);

    // 管理 per-client_fd 订阅集合。
    bool add_subscription(int client_fd, uint32_t event_type);
    bool remove_subscription(int client_fd, uint32_t event_type);

private:
    class Impl;
    std::shared_ptr<Impl> m_impl;
};

// ============================================================
// Subscription - RAII 订阅句柄
// ============================================================
class Subscription {
public:
    Subscription();
    ~Subscription();

    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    // 主动取消订阅，停止读取线程并关闭连接。
    void cancel();

    // 是否仍在活跃接收事件。
    bool isActive() const;

private:
    class Impl;
    std::shared_ptr<Impl> m_impl;

    friend class Client;
    explicit Subscription(std::shared_ptr<Impl> impl);
};

// ============================================================
// Client - 请求-响应 + 订阅客户端 facade
// ============================================================
class Client {
public:
    Client(const std::string& socket_path, const IpcConfig& config = IpcConfig{});
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    bool connect();
    void disconnect();
    bool is_connected() const;

    // 请求-响应调用，支持惰性连接。
    // 返回 (status_code, response_json)：
    //   status_code == 0  -> 成功
    //   status_code > 0   -> 服务端处理错误 (FW-03xx)
    //   status_code < 0   -> 客户端传输错误 (-FW-03xx)
    // 传输失败后自动重连并重试一次。
    std::pair<int32_t, std::string> call(
        uint32_t method_id, std::string_view params_json);

    // 请求-响应调用（单次，不自动重试）。
    // 用于非幂等/一次性操作（如 getSeed/verifyKey），
    // 传输失败后不重放请求，调用方需将结果视为 unknown outcome。
    std::pair<int32_t, std::string> callOnce(
        uint32_t method_id, std::string_view params_json);

    // 订阅事件。每次订阅建立独立连接，完成 Response 握手后进入 event-only。
    // 返回 RAII Subscription 句柄，析构时自动取消。
    Subscription subscribe(uint32_t method_id, uint32_t event_type,
                           EventCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ipc
} // namespace fw
} // namespace tbox
