#pragma once

#include "ipc_types.h"
#include <string>
#include <cstdint>

namespace tbox {
namespace fw {
namespace ipc {

// ============================================================
// Protocol - 集中实现帧编解码、整帧读写和 base64
// ============================================================
class Protocol {
public:
    // ---- Encode (header + payload -> byte buffer) ----
    static std::string encodeRequest(uint32_t method_id,
                                     const std::string& params_json);
    static std::string encodeResponse(int32_t status_code,
                                      const std::string& response_json);
    static std::string encodeEvent(uint32_t event_type,
                                   const std::string& payload_json);

    // ---- Read complete frame from socket ----
    // 返回 false 表示传输失败（EOF/超时/帧非法）。
    static bool readRequest(int fd, uint32_t& method_id,
                            std::string& params_json,
                            uint32_t max_frame_bytes);
    static bool readResponse(int fd, int32_t& status_code,
                             std::string& response_json,
                             uint32_t max_frame_bytes);
    static bool readEvent(int fd, uint32_t& event_type,
                          std::string& payload_json,
                          uint32_t max_frame_bytes);

    // ---- Write complete frame to socket ----
    static bool writeRequest(int fd, uint32_t method_id,
                             const std::string& params_json);
    static bool writeResponse(int fd, int32_t status_code,
                              const std::string& response_json);
    static bool writeEvent(int fd, uint32_t event_type,
                           const std::string& payload_json);

    // ---- Low-level socket I/O ----
    // readExact: 循环读取至完整 size 字节，处理短读、EINTR、EOF、超时。
    static bool readExact(int fd, void* buffer, size_t size);
    // writeAll: 循环写入至完整 size 字节，处理短写、EINTR、EPIPE。
    static bool writeAll(int fd, const void* data, size_t size);

    // ---- Base64 ----
    static std::string base64Encode(const std::string& data);
    static std::string base64Decode(const std::string& encoded);

    // ---- Validation ----
    static bool validateLength(uint32_t length, uint32_t max);
};

} // namespace ipc
} // namespace fw
} // namespace tbox
