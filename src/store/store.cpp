#include "store.h"
#include "store_path_resolver.h"
#include "store_serializer.h"
#include "store_atomic_writer.h"
#include "store_file_lock.h"
#include "store_flush_policy.h"
#include <fstream>
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>
#include <unistd.h>

namespace hwyz {
namespace store {

class Store::Impl {
public:
    Impl(const std::string& serviceName, const std::string& storeRoot)
        : m_pathResolver(serviceName, storeRoot)
        , m_atomicWriter(m_pathResolver)
        , m_fileLock(m_pathResolver)
    {
        m_ready = m_pathResolver.ensureDirectory();
    }

    ~Impl() = default;

    template<typename T>
    void save(const std::string& key, const T& value) {
        if (!m_ready) {
            throw StoreException(StoreError::kPathUnavailable,
                                 "Store not ready", key);
        }

        if (!m_fileLock.acquire(key, 1000)) {
            throw StoreException(StoreError::kLockFailed,
                                 "Failed to acquire lock", key);
        }

        std::string bytes = m_serializer.serialize(value);
        if (bytes.empty()) {
            m_fileLock.release(key);
            throw StoreException(StoreError::kSerializationFailed,
                                 "Failed to serialize value", key);
        }

        if (!m_atomicWriter.write(key, bytes)) {
            m_fileLock.release(key);
            throw StoreException(StoreError::kAtomicWriteFailed,
                                 "Failed to write atomically", key);
        }

        m_flushPolicy.clearDirty(key);
        m_fileLock.release(key);
    }

    template<typename T>
    T load(const std::string& key) const {
        if (!m_ready) {
            throw StoreException(StoreError::kPathUnavailable,
                                 "Store not ready", key);
        }

        if (!m_atomicWriter.exists(key)) {
            throw StoreException(StoreError::kKeyNotFound,
                                 "Key not found", key);
        }

        std::string filePath = m_pathResolver.getKeyPath(key);
        std::ifstream ifs(filePath);
        if (!ifs.is_open()) {
            throw StoreException(StoreError::kAtomicWriteFailed,
                                 "Failed to open file", key);
        }

        std::string bytes((std::istreambuf_iterator<char>(ifs)),
                          std::istreambuf_iterator<char>());

        T value;
        if (!m_serializer.deserialize(bytes, value)) {
            throw StoreException(StoreError::kSerializationFailed,
                                 "Failed to deserialize value", key);
        }

        return value;
    }

    template<typename T>
    T loadOr(const std::string& key, const T& defaultValue) const {
        try {
            return load<T>(key);
        } catch (const StoreException& e) {
            if (e.getError().code == StoreError::kKeyNotFound) {
                return defaultValue;
            }
            throw;
        }
    }

    bool has(const std::string& key) const {
        if (!m_ready) {
            return false;
        }
        return m_atomicWriter.exists(key);
    }

    void remove(const std::string& key) {
        if (!m_ready) {
            throw StoreException(StoreError::kPathUnavailable,
                                 "Store not ready", key);
        }

        if (!m_fileLock.acquire(key, 1000)) {
            throw StoreException(StoreError::kLockFailed,
                                 "Failed to acquire lock", key);
        }

        m_atomicWriter.remove(key);
        m_flushPolicy.clearDirty(key);
        m_fileLock.release(key);
    }

    void flush() {
        m_flushPolicy.reset();
    }

    bool isReady() const {
        return m_ready;
    }

    void cleanup() {
        if (!m_ready) {
            return;
        }

        std::string storePath = m_pathResolver.getStorePath();
        DIR* dir = opendir(storePath.c_str());
        if (dir == nullptr) {
            return;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            std::string fullPath = storePath + name;
            std::remove(fullPath.c_str());
        }
        closedir(dir);

        rmdir(storePath.c_str());
        std::string rootPath = storePath.substr(0, storePath.find_last_of("/", storePath.size() - 2) + 1);
        rmdir(rootPath.c_str());

        m_ready = false;
    }

private:
    PathResolver m_pathResolver;
    Serializer m_serializer;
    AtomicWriter m_atomicWriter;
    FileLock m_fileLock;
    FlushPolicy m_flushPolicy;
    bool m_ready = false;
};

Store Store::open(const std::string& serviceName, const std::string& storeRoot) {
    Store store;
    store.m_impl.reset(new Impl(serviceName, storeRoot));
    return store;
}

Store::Store() = default;
Store::~Store() = default;
Store::Store(Store&& other) = default;
Store& Store::operator=(Store&& other) = default;

template<typename T>
void Store::save(const std::string& key, const T& value) {
    m_impl->save<T>(key, value);
}

template<typename T>
T Store::load(const std::string& key) const {
    return m_impl->load<T>(key);
}

template<typename T>
T Store::loadOr(const std::string& key, const T& defaultValue) const {
    return m_impl->loadOr<T>(key, defaultValue);
}

bool Store::has(const std::string& key) const {
    return m_impl->has(key);
}

void Store::remove(const std::string& key) {
    m_impl->remove(key);
}

void Store::flush() {
    m_impl->flush();
}

bool Store::isReady() const {
    return m_impl->isReady();
}

void Store::cleanup() {
    m_impl->cleanup();
}

template void Store::save<int>(const std::string&, const int&);
template void Store::save<double>(const std::string&, const double&);
template void Store::save<bool>(const std::string&, const bool&);
template void Store::save<std::string>(const std::string&, const std::string&);

template int Store::load<int>(const std::string&) const;
template double Store::load<double>(const std::string&) const;
template bool Store::load<bool>(const std::string&) const;
template std::string Store::load<std::string>(const std::string&) const;

template int Store::loadOr<int>(const std::string&, const int&) const;
template double Store::loadOr<double>(const std::string&, const double&) const;
template bool Store::loadOr<bool>(const std::string&, const bool&) const;
template std::string Store::loadOr<std::string>(const std::string&, const std::string&) const;

} // namespace store
} // namespace hwyz
