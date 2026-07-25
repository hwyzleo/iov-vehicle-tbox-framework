#pragma once

#include "store_types.h"
#include <string>
#include <memory>

namespace hwyz {
namespace store {

class Store {
public:
    ~Store();
    Store(Store&& other);
    Store& operator=(Store&& other);

    static Store open(const std::string& serviceName,
                      const std::string& storeRoot = "/var/lib/tbox");

    template<typename T>
    void save(const std::string& key, const T& value);

    template<typename T>
    T load(const std::string& key) const;

    template<typename T>
    T loadOr(const std::string& key, const T& defaultValue) const;

    bool has(const std::string& key) const;

    void remove(const std::string& key);

    void flush();

    bool isReady() const;

    void cleanup();

private:
    Store();
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace store
} // namespace hwyz
