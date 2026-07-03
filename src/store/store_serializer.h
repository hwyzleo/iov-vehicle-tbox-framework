#pragma once

#include <string>
#include <sstream>
#include <type_traits>

namespace hwyz {
namespace store {

class Serializer {
public:
    Serializer() = default;
    ~Serializer() = default;

    template<typename T>
    std::string serialize(const T& value) const {
        static_assert(std::is_arithmetic<T>::value || std::is_same<T, std::string>::value,
                      "Type must be arithmetic or std::string");

        std::ostringstream oss;
        oss << value;
        return oss.str();
    }

    template<typename T>
    bool deserialize(const std::string& bytes, T& value) const {
        static_assert(std::is_arithmetic<T>::value || std::is_same<T, std::string>::value,
                      "Type must be arithmetic or std::string");

        if (bytes.empty()) {
            return false;
        }

        std::istringstream iss(bytes);
        iss >> value;
        return !iss.fail() || std::is_same<T, std::string>::value;
    }

    template<typename T>
    std::string getTypeName() const {
        if (std::is_same<T, int>::value) return "int";
        if (std::is_same<T, double>::value) return "double";
        if (std::is_same<T, bool>::value) return "bool";
        if (std::is_same<T, std::string>::value) return "string";
        return "unknown";
    }
};

template<>
std::string Serializer::serialize<std::string>(const std::string& value) const;

template<>
bool Serializer::deserialize<std::string>(const std::string& bytes, std::string& value) const;

} // namespace store
} // namespace hwyz
