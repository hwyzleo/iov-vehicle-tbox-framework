#include "ipc_protocol.h"
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>

namespace tbox {
namespace fw {
namespace ipc {

// ============================================================
// Encode
// ============================================================

std::string Protocol::encodeRequest(uint32_t method_id,
                                    const std::string& params_json) {
    RequestHeader header;
    header.method_id = method_id;
    header.params_length = static_cast<uint32_t>(params_json.size());

    std::string frame;
    frame.reserve(sizeof(header) + params_json.size());
    frame.append(reinterpret_cast<const char*>(&header), sizeof(header));
    frame.append(params_json);
    return frame;
}

std::string Protocol::encodeResponse(int32_t status_code,
                                     const std::string& response_json) {
    ResponseHeader header;
    header.status_code = status_code;
    header.data_length = static_cast<uint32_t>(response_json.size());

    std::string frame;
    frame.reserve(sizeof(header) + response_json.size());
    frame.append(reinterpret_cast<const char*>(&header), sizeof(header));
    frame.append(response_json);
    return frame;
}

std::string Protocol::encodeEvent(uint32_t event_type,
                                  const std::string& payload_json) {
    EventHeader header;
    header.event_type = event_type;
    header.payload_length = static_cast<uint32_t>(payload_json.size());

    std::string frame;
    frame.reserve(sizeof(header) + payload_json.size());
    frame.append(reinterpret_cast<const char*>(&header), sizeof(header));
    frame.append(payload_json);
    return frame;
}

// ============================================================
// Read complete frame from socket
// ============================================================

bool Protocol::readRequest(int fd, uint32_t& method_id,
                           std::string& params_json,
                           uint32_t max_frame_bytes) {
    RequestHeader header;
    if (!readExact(fd, &header, sizeof(header))) {
        return false;
    }
    if (!validateLength(header.params_length, max_frame_bytes)) {
        return false;
    }
    params_json.resize(header.params_length);
    if (header.params_length > 0) {
        if (!readExact(fd, &params_json[0], header.params_length)) {
            return false;
        }
    }
    method_id = header.method_id;
    return true;
}

bool Protocol::readResponse(int fd, int32_t& status_code,
                            std::string& response_json,
                            uint32_t max_frame_bytes) {
    ResponseHeader header;
    if (!readExact(fd, &header, sizeof(header))) {
        return false;
    }
    if (!validateLength(header.data_length, max_frame_bytes)) {
        return false;
    }
    response_json.resize(header.data_length);
    if (header.data_length > 0) {
        if (!readExact(fd, &response_json[0], header.data_length)) {
            return false;
        }
    }
    status_code = header.status_code;
    return true;
}

bool Protocol::readEvent(int fd, uint32_t& event_type,
                         std::string& payload_json,
                         uint32_t max_frame_bytes) {
    EventHeader header;
    if (!readExact(fd, &header, sizeof(header))) {
        return false;
    }
    if (!validateLength(header.payload_length, max_frame_bytes)) {
        return false;
    }
    payload_json.resize(header.payload_length);
    if (header.payload_length > 0) {
        if (!readExact(fd, &payload_json[0], header.payload_length)) {
            return false;
        }
    }
    event_type = header.event_type;
    return true;
}

// ============================================================
// Write complete frame to socket
// ============================================================

bool Protocol::writeRequest(int fd, uint32_t method_id,
                            const std::string& params_json) {
    std::string frame = encodeRequest(method_id, params_json);
    return writeAll(fd, frame.data(), frame.size());
}

bool Protocol::writeResponse(int fd, int32_t status_code,
                             const std::string& response_json) {
    std::string frame = encodeResponse(status_code, response_json);
    return writeAll(fd, frame.data(), frame.size());
}

bool Protocol::writeEvent(int fd, uint32_t event_type,
                          const std::string& payload_json) {
    std::string frame = encodeEvent(event_type, payload_json);
    return writeAll(fd, frame.data(), frame.size());
}

// ============================================================
// Low-level socket I/O
// ============================================================

bool Protocol::readExact(int fd, void* buffer, size_t size) {
    uint8_t* ptr = static_cast<uint8_t*>(buffer);
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t n = recv(fd, ptr, remaining, 0);
        if (n > 0) {
            ptr += n;
            remaining -= static_cast<size_t>(n);
        } else if (n == 0) {
            // EOF - connection closed by peer
            return false;
        } else {
            // n < 0
            if (errno == EINTR) {
                continue;
            }
            // EAGAIN/EWOULDBLOCK (timeout) or other error
            return false;
        }
    }
    return true;
}

bool Protocol::writeAll(int fd, const void* data, size_t size) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t n = send(fd, ptr, remaining, 0);
        if (n > 0) {
            ptr += n;
            remaining -= static_cast<size_t>(n);
        } else if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            // EPIPE, EAGAIN or other error
            return false;
        } else {
            // n == 0 - unusual, treat as error
            return false;
        }
    }
    return true;
}

// ============================================================
// Base64
// ============================================================

static const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string Protocol::base64Encode(const std::string& data) {
    std::string result;
    size_t in_len = data.size();
    result.reserve(((in_len + 2) / 3) * 4);

    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    const unsigned char* src = reinterpret_cast<const unsigned char*>(data.data());
    size_t idx = 0;

    while (idx < in_len) {
        char_array_3[i++] = src[idx++];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++) {
                result += kBase64Chars[char_array_4[i]];
            }
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; j < i + 1; j++) {
            result += kBase64Chars[char_array_4[j]];
        }

        while (i++ < 3) {
            result += '=';
        }
    }

    return result;
}

std::string Protocol::base64Decode(const std::string& encoded) {
    size_t in_len = encoded.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string result;

    while (in_len-- && (encoded[in_] != '=') &&
           (isalnum(static_cast<unsigned char>(encoded[in_])) ||
            (encoded[in_] == '+') || (encoded[in_] == '/'))) {
        char_array_4[i++] = static_cast<unsigned char>(encoded[in_]);
        in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++) {
                const char* p = strchr(kBase64Chars, char_array_4[i]);
                if (p == nullptr) {
                    return "";  // Invalid input, no partial result
                }
                char_array_4[i] = static_cast<unsigned char>(p - kBase64Chars);
            }

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0x0f) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + char_array_4[3];

            for (i = 0; i < 3; i++) {
                result += static_cast<char>(char_array_3[i]);
            }
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 4; j++) {
            char_array_4[j] = 0;
        }

        for (j = 0; j < 4; j++) {
            const char* p = strchr(kBase64Chars, char_array_4[j]);
            if (p != nullptr) {
                char_array_4[j] = static_cast<unsigned char>(p - kBase64Chars);
            } else {
                char_array_4[j] = 0;
            }
        }

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0x0f) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + char_array_4[3];

        for (j = 0; j < i - 1; j++) {
            result += static_cast<char>(char_array_3[j]);
        }
    }

    return result;
}

// ============================================================
// Validation
// ============================================================

bool Protocol::validateLength(uint32_t length, uint32_t max) {
    return length <= max;
}

} // namespace ipc
} // namespace fw
} // namespace tbox
