//
// Created by hwyz_leo on 2025/8/6.
//

#ifndef FRAMEWORK_UTILS_H
#define FRAMEWORK_UTILS_H

#include <iostream>
#include <fstream>
#include <mutex>

namespace hwyz {
    static const std::string base64_chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";

    // 全局键值
    enum global_key_t {
        VIN = 1, // 车架号
        CURRENT_ICCID = 2, // 当前ICCID
        BATTERY_PACK_CODE = 3, // 电池包编号
    };

    class Utils {
    public:
        Utils() {};

        ~Utils() {};

    public:
        /**
        * 判断文件是否存在
        * @param file_path 文件路径
        * @return 文件是否存在
        */
        static bool file_exists(const std::string &file_path);

        /**
        * 写入文件
        * @param file_path 文件路径
        * @param data 写入数据
        * @return 是否成功
        */
        static bool write_file(const std::string &file_path, const std::string &data);

        /**
        * 重命名文件
        * 新文件路径有文件也会被覆盖
        * @param old_path 原文件路径
        * @param new_path 新文件路径
        * @return 是否成功
        */
        static bool rename_file(const std::string &old_path, const std::string &new_path);

        /**
        * 获取当前日期(yyyymmdd)
        * @return 当前日期
        */
        static std::string get_current_date();

        /**
        * 十六进制字符串转字节数组
        * @param hex_str 十六进制字符串
        * @return 字节数组
        */
        static std::vector<unsigned char> hex_to_bytes(const std::string &hex_str);

        /**
        * 字节数组转十六进制字符串
        * @param bytes 字节数组
        * @return 十六进制字符串
        */
        static std::string bytes_to_hex(const std::vector<unsigned char> &bytes, bool uppercase);

        /**
        * Base64编码
        * @param input 输入字符串
        * @return Base64编码后的字符串
        */
        static std::string base64_encode(const std::string &input);

        /**
        * Base64解码
        * @param input 输入字符串
        * @return Base64解码后的字符串
        */
        static std::string base64_decode(const std::string &input);

        /**
        * AES解密
        * @param encrypted_bytes 加密字节数组
        * @param key 密钥
        * @param iv IV
        * @return 解密后字节数组
        */
        static std::vector<unsigned char> aes_decrypt(const std::vector<unsigned char> &encrypted_bytes,
                                                      const std::vector<unsigned char> &key,
                                                      const std::vector<unsigned char> &iv);

        /**
         * 全局写入字符串
         * @param global_key key
         * @param value 值
         */
        static void global_write_string(global_key_t global_key, const std::string& value);

        /**
         * 全局读取字符串
         * @param global_key 全局key
         */
        static std::string global_read_string(global_key_t global_key);

    private:
        // 全局KEY锁
        static std::mutex global_key_mutex_;
        // 全局KEY文件路径
        static std::string global_key_file_path_;
    };
}

#endif //FRAMEWORK_UTILS_H