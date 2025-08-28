//
// Created by hwyz_leo on 2025/8/6.
//
#include <fstream>
#include <cerrno>
#include <cstring>
#include <map>

#include "spdlog/spdlog.h"
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#include "utils.h"

#ifdef _WIN32
#include <io.h>
#define access _access
#else

#include <unistd.h>

#endif

namespace hwyz {

    std::mutex Utils::global_key_mutex_;
    std::string Utils::global_key_file_path_ = "/tmp/global_key.dat";

    bool Utils::file_exists(const std::string &file_path) {
        return (access(file_path.c_str(), F_OK) == 0);
    }

    bool Utils::write_file(const std::string &file_path, const std::string &data) {
        std::ofstream file(file_path);
        if (file.is_open()) {
            file << data;
            file.close();
            return true;
        }
        spdlog::warn("写入文件[{}]失败", file_path);
        return false;
    }

    bool Utils::rename_file(const std::string &old_path, const std::string &new_path) {
#ifdef _WIN32
        // Windows不会覆盖，所以需要先删除目标文件
        if (std::remove(new_path.c_str()) != 0 && errno != ENOENT) {
            spdlog::warn("删除新文件路径[{}]失败", new_path);
            return false;
        }
#endif
        int result = std::rename(old_path.c_str(), new_path.c_str());
        if (result != 0) {
            spdlog::warn("重命名文件[{} -> {}]失败", old_path, new_path);
            return false;
        }
        return true;
    }

    std::string Utils::get_current_date() {
        auto now = std::chrono::system_clock::now();
        std::time_t current_time = std::chrono::system_clock::to_time_t(now);
        struct std::tm *local_time = std::localtime(&current_time);
        std::string date;
        int year = local_time->tm_year + 1900;
        date = std::to_string(year);
        int month = local_time->tm_mon + 1;
        if (month < 10) {
            date += "0";
        }
        date += std::to_string(month);
        int day = local_time->tm_mday;
        if (day < 10) {
            date += "0";
        }
        date += std::to_string(day);
        return date;
    }

    long long Utils::get_current_timestamp_sec() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
    }

    long long Utils::get_current_timestamp_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
    }

    long long Utils::get_current_timestamp_us() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::vector<unsigned char> Utils::hex_to_bytes(const std::string &hex_str) {
        if (hex_str.length() % 2 != 0) {
            throw std::invalid_argument("Hex string length must be even");
        }

        std::vector<unsigned char> bytes;
        bytes.reserve(hex_str.length() / 2);

        for (size_t i = 0; i < hex_str.length(); i += 2) {
            if (!std::isxdigit(hex_str[i]) || !std::isxdigit(hex_str[i + 1])) {
                throw std::invalid_argument("Invalid hex character");
            }

            try {
                std::string byte_string = hex_str.substr(i, 2);
                unsigned char byte = (unsigned char) std::stoi(byte_string, nullptr, 16);
                bytes.push_back(byte);
            } catch (const std::exception &e) {
                throw std::runtime_error("Failed to convert hex to bytes: " + std::string(e.what()));
            }
        }

        return bytes;
    }

    std::string Utils::bytes_to_hex(const std::vector<unsigned char> &bytes, bool uppercase) {
        static const char hex_chars_upper[] = "0123456789ABCDEF";
        static const char hex_chars_lower[] = "0123456789abcdef";
        const char *hex_chars = uppercase ? hex_chars_upper : hex_chars_lower;

        std::string hex_str;
        hex_str.reserve(bytes.size() * 2);

        for (unsigned char byte: bytes) {
            hex_str.push_back(hex_chars[byte >> 4]);
            hex_str.push_back(hex_chars[byte & 0xF]);
        }

        return hex_str;
    }

    std::string Utils::base64_encode(const std::string &input) {
        BIO *bio, *b64;
        BUF_MEM *bufferPtr;

        b64 = BIO_new(BIO_f_base64());
        bio = BIO_new(BIO_s_mem());
        bio = BIO_push(b64, bio);

        // 不要在编码的输出中插入换行符
        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

        BIO_write(bio, input.c_str(), input.length());
        BIO_flush(bio);
        BIO_get_mem_ptr(bio, &bufferPtr);

        std::string result(bufferPtr->data, bufferPtr->length);
        BIO_free_all(bio);

        return result;
    }

    std::string Utils::base64_decode(const std::string &input) {
        BIO *bio, *b64;

        int decodeLen = input.length();
        char* buffer = new char[decodeLen + 1];
        buffer[decodeLen] = '\0';

        bio = BIO_new_mem_buf(input.c_str(), -1);
        b64 = BIO_new(BIO_f_base64());
        bio = BIO_push(b64, bio);

        // 不要期望换行符
        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

        int length = BIO_read(bio, buffer, decodeLen);
        BIO_free_all(bio);

        std::string result(buffer, length);
        delete[] buffer;

        return result;
    }

    std::vector<unsigned char>
    Utils::aes_decrypt(const std::vector<unsigned char> &encrypted_bytes, const std::vector<unsigned char> &key,
                       const std::vector<unsigned char> &iv) {
        if (key.size() != 16) {
            throw std::runtime_error("密钥长度无效");
        }
        if (iv.size() != AES_BLOCK_SIZE) {
            throw std::runtime_error("IV长度无效");
        }
        if (encrypted_bytes.empty() || encrypted_bytes.size() % AES_BLOCK_SIZE != 0) {
            throw std::runtime_error("加密数据长度无效");
        }

        std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>
                ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
        if (!ctx) {
            throw std::runtime_error("创建密钥环境失败");
        }
        std::vector<unsigned char> plaintext(encrypted_bytes.size());
        int len;
        int plaintext_len = 0;

        if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr,
                               key.data(), iv.data()) != 1) {
            throw std::runtime_error("解密初始化失败");
        }
        if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len,
                              encrypted_bytes.data(), encrypted_bytes.size()) != 1) {
            throw std::runtime_error("解密数据失败");
        }
        plaintext_len += len;
        if (EVP_DecryptFinal_ex(ctx.get(),
                                plaintext.data() + plaintext_len, &len) != 1) {
            throw std::runtime_error("解密最后步骤失败");
        }
        plaintext_len += len;
        plaintext.resize(plaintext_len);
        return plaintext;
    }

    void Utils::global_write_string(global_key_t global_key, const std::string &value) {
        std::lock_guard<std::mutex> lock(global_key_mutex_);
        std::map<int, std::string> kv_data;
        std::ifstream read_file(global_key_file_path_);
        std::string line;
        while (std::getline(read_file, line)) {
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                try {
                    int key = std::stoi(line.substr(0, pos));
                    std::string existing_value = line.substr(pos + 1);
                    kv_data[key] = existing_value;
                } catch (...) {
                }
            }
        }
        read_file.close();
        kv_data[global_key] = value;
        std::ofstream write_file(global_key_file_path_, std::ios::trunc);
        for (const auto &pair: kv_data) {
            write_file << pair.first << "=" << pair.second << std::endl;
        }
    }

    std::string Utils::global_read_string(global_key_t global_key) {
        std::lock_guard<std::mutex> lock(global_key_mutex_);
        std::ifstream file(global_key_file_path_);
        std::string line;
        while (std::getline(file, line)) {
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                try {
                    int key = std::stoi(line.substr(0, pos));
                    if (key == global_key) {
                        return line.substr(pos + 1);
                    }
                } catch (...) {
                }
            }
        }
        return "";
    }
}