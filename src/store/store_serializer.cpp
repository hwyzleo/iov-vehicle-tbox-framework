#include "store_serializer.h"

namespace hwyz {
namespace store {

template<>
std::string Serializer::serialize<std::string>(const std::string& value) const {
    return value;
}

template<>
bool Serializer::deserialize<std::string>(const std::string& bytes, std::string& value) const {
    value = bytes;
    return true;
}

} // namespace store
} // namespace hwyz
