# iov-vehicle-tbox-framework 使用指南

本文档说明如何在其他 TBOX 服务中集成和使用 `iov-vehicle-tbox-framework` 框架。

---

## 1. 概述

`iov-vehicle-tbox-framework` 是 TBOX 车端基础框架库（`libhwyz`），提供以下核心能力：

| 模块 | 头文件 | 说明 |
|------|--------|------|
| 应用生命周期 | `application.h` | 服务进程基类，提供启动、信号处理、优雅退出 |
| 配置管理 | `config.h` / `config_types.h` | 分层 YAML 配置加载、合并、校验、不可变快照 |
| 工具集 | `utils.h` | 文件操作、时间戳、编解码、AES 解密、全局键值存储 |
| 持久化键 | `persist_key.h` | 持久化存储的 key 常量定义 |

---

## 2. 构建集成

### 2.1 CMake 方式（推荐）

在服务的 `CMakeLists.txt` 中：

```cmake
find_package(HWYZ REQUIRED)
target_link_libraries(your_service PRIVATE HWYZ::hwyz)
```

### 2.2 pkg-config 方式

```bash
g++ main.cpp $(pkg-config --cflags --libs hwyz) -o your_service
```

### 2.3 依赖项

框架会自动引入以下依赖，无需单独配置：

| 库 | 用途 |
|----|------|
| yaml-cpp | YAML 解析 |
| spdlog | 日志（头文件库，随框架头文件一起安装） |
| OpenSSL | AES 解密、Base64 编解码 |

---

## 3. 快速开始

### 3.1 创建服务入口

继承 `hwyz::Application` 并使用 `APPLICATION_ENTRY` 宏生成 `main` 函数：

```cpp
#include "application.h"
#include "config.h"

class MyService : public hwyz::Application {
protected:
    // 可选：初始化阶段，返回 false 将阻止 execute() 执行
    bool initialize() override {
        // 加载配置（见第 4 节）
        auto err = CONFIG_MANAGER.load("myservice");
        if (err != hwyz::config::ConfigError::kOk) {
            return false;
        }
        return true;
    }

    // 必须实现：服务主逻辑
    int execute() override {
        auto cfg = CONFIG_SNAPSHOT;
        // 业务逻辑...
        return 0;
    }

    // 可选：清理阶段
    void cleanup() override {
        // 释放资源
    }
};

APPLICATION_ENTRY(MyService)
```

`APPLICATION_ENTRY(MyService)` 宏会展开为标准 `main` 函数，自动完成：
- 调用 `run()` 启动应用
- 异常捕获与错误输出

---

## 4. 配置管理（重点）

### 4.1 配置文件分层体系

框架采用三层 YAML 配置，后加载的层覆盖先加载的同名字段：

| 层级 | 枚举值 | 文件路径 | 是否必需 |
|------|--------|----------|----------|
| Common | `kCommon` | `/etc/tbox/common.yaml` | 是 |
| Service | `kService` | `/etc/tbox/conf.d/<serviceName>.yaml` | 否 |
| Local | `kLocal` | `./<serviceName>.yaml`（当前工作目录） | 否 |

**合并规则：**
- Map 类型：递归深度合并
- Scalar / Sequence 类型：后层直接覆盖前层

### 4.2 加载配置

```cpp
#include "config.h"

// 使用默认配置根目录 /etc/tbox/
auto err = CONFIG_MANAGER.load("myservice");

// 指定自定义配置根目录
auto err = CONFIG_MANAGER.load("myservice", "/opt/tbox/config/");

// 检查加载结果
if (err != hwyz::config::ConfigError::kOk) {
    auto info = CONFIG_MANAGER.getLastError();
    // info.code    - 错误码
    // info.message - 错误描述
    // info.path    - 出错的配置路径
}
```

### 4.3 读取配置

配置加载后通过**不可变快照**（`ImmutableConfigView`）读取，快照是线程安全的：

```cpp
// 获取快照
auto cfg = CONFIG_SNAPSHOT;

// ---- 标量读取 ----
std::string host = cfg->getString("server.host", "127.0.0.1");
int port         = cfg->getInt("server.port", 8080);
double timeout   = cfg->getDouble("network.timeout", 30.0);
bool debug       = cfg->getBool("log.debug", false);

// ---- 检查键是否存在 ----
if (cfg->has("tls.cert_path")) {
    std::string cert = cfg->getString("tls.cert_path");
}

// ---- 读取列表 ----
std::vector<std::string> endpoints = cfg->getStringList("mqtt.endpoints");

// ---- 获取嵌套配置节 ----
auto logCfg = cfg->getSection("log");
std::string level = logCfg->getString("level", "info");

// ---- 遍历所有顶层键 ----
for (const auto& key : cfg->getKeys()) {
    // ...
}
```

**键路径使用点分表示法**，例如 `"server.host"` 对应 YAML：

```yaml
server:
  host: 192.168.1.100
  port: 8080
```

### 4.4 错误码说明

| 错误码 | 枚举值 | 含义 |
|--------|--------|------|
| 0 | `kOk` | 成功 |
| 1 | `kFileNotFound` | 必需的配置文件不存在 |
| 2 | `kParseFailed` | YAML 语法解析失败 |
| 3 | `kValidationFailed` | 校验失败（类型不匹配或必填项缺失） |
| 4 | `kMergeConflict` | 层级合并冲突 |

### 4.5 配置文件示例

**`/etc/tbox/common.yaml`**（公共配置）：

```yaml
log:
  level: info
  file: /var/log/tbox/tbox.log

network:
  timeout: 30.0
  retry_count: 3
```

**`/etc/tbox/conf.d/myservice.yaml`**（服务专属配置）：

```yaml
server:
  host: 192.168.1.100
  port: 8080

log:
  level: debug   # 覆盖 common.yaml 中的 log.level
```

最终 `myservice` 读取到的 `log.level` 为 `"debug"`。

---

## 5. 工具类（Utils）

`hwyz::Utils` 提供静态方法，无需实例化：

```cpp
#include "utils.h"

// 文件操作
bool exists = hwyz::Utils::file_exists("/tmp/test.txt");
hwyz::Utils::write_file("/tmp/test.txt", "hello");
hwyz::Utils::rename_file("/tmp/old.txt", "/tmp/new.txt");

// 时间
std::string date = hwyz::Utils::get_current_date();           // "20260702"
long long ts_sec = hwyz::Utils::get_current_timestamp_sec();  // 秒级
long long ts_ms  = hwyz::Utils::get_current_timestamp_ms();   // 毫秒级
long long ts_us  = hwyz::Utils::get_current_timestamp_us();   // 微秒级

// 编解码
auto bytes = hwyz::Utils::hex_to_bytes("48656C6C6F");
std::string hex = hwyz::Utils::bytes_to_hex(bytes, true);
std::string b64 = hwyz::Utils::base64_encode("Hello");
std::string raw = hwyz::Utils::base64_decode(b64);

// AES-128-CBC 解密
auto decrypted = hwyz::Utils::aes_decrypt(encrypted_bytes, key, iv);

// 全局键值存储（跨模块共享运行时数据）
hwyz::Utils::global_write_string(hwyz::VIN, "LSGJA52U7SA123456");
std::string vin = hwyz::Utils::global_read_string(hwyz::VIN);
```

### 全局键枚举

| 枚举值 | 说明 |
|--------|------|
| `hwyz::VIN` | 车架号 |
| `hwyz::CURRENT_ICCID` | 当前 ICCID |
| `hwyz::BATTERY_PACK_SN` | 电池包序列号 |
| `hwyz::TBOX_SN` | TBOX 序列号 |
| `hwyz::TBOX_MCU_VERSION` | TBOX MCU 版本 |

---

## 6. 本地持久化（Store）

`framework-store` 提供原子、掉电安全的本地写盘能力，与配置管理并列，互不复用。

### 6.1 基本用法

```cpp
#include "store.h"

// 打开存储（使用默认根路径 /var/lib/tbox）
hwyz::store::Store store = hwyz::store::Store::open("prov");

// 保存数据
store.save<int>("counter", 42);
store.save<std::string>("session.id", "abc123");

// 加载数据
int counter = store.load<int>("counter");
std::string sessionId = store.loadOr<std::string>("session.id", "default");

// 检查与删除
if (store.has("counter")) {
    store.remove("counter");
}
```

### 6.2 从配置中读取持久化根路径

各服务应先加载配置，从 `common.store.root` 读取持久化根路径，再注入 `Store::open`：

```cpp
#include "config.h"
#include "store.h"

// 1. 加载配置
auto err = CONFIG_MANAGER.load("prov");
if (err != hwyz::config::ConfigError::kOk) {
    // 处理错误
}

// 2. 获取配置快照
auto cfg = CONFIG_SNAPSHOT;

// 3. 读取持久化根路径（未配置时使用默认值 /var/lib/tbox）
std::string storeRoot = cfg->getString("common.store.root", "/var/lib/tbox");

// 4. 打开存储，注入根路径
hwyz::store::Store store = hwyz::store::Store::open("prov", storeRoot);
```

**设计说明**：`framework-store` 不依赖 `framework-config`，根路径由服务注入，保持两个组件解耦。

### 6.3 错误处理

```cpp
try {
    store.save<int>("key", 42);
} catch (const hwyz::store::StoreException& e) {
    auto error = e.getError();
    switch (error.code) {
        case hwyz::store::StoreError::kPathUnavailable:
            // 存储路径不可用
            break;
        case hwyz::store::StoreError::kAtomicWriteFailed:
            // 原子写失败
            break;
        case hwyz::store::StoreError::kLockFailed:
            // 并发锁获取失败
            break;
        case hwyz::store::StoreError::kSerializationFailed:
            // 序列化失败
            break;
        case hwyz::store::StoreError::kKeyNotFound:
            // key不存在
            break;
    }
}
```

## 7. Application 生命周期

`hwyz::Application` 管理服务进程的完整生命周期：

```
构造 -> run() -> initialize() -> execute() -> cleanup() -> 析构
                                      |
                          (SIGINT/SIGTERM 触发优雅退出)
```

- `initialize()`：初始化阶段，可加载配置、建立连接等，返回 `false` 将中止执行
- `execute()`：主业务逻辑，必须实现，返回值作为进程退出码
- `cleanup()`：清理阶段，释放资源

框架自动处理以下信号：`SIGINT`、`SIGTERM`、`SIGABRT`、`SIGSEGV`

---

## 8. 完整服务示例

```cpp
#include "application.h"
#include "config.h"
#include "store.h"
#include "utils.h"
#include <iostream>

class ProvService : public hwyz::Application {
protected:
    bool initialize() override {
        // 加载配置
        auto err = CONFIG_MANAGER.load("prov");
        if (err != hwyz::config::ConfigError::kOk) {
            auto info = CONFIG_MANAGER.getLastError();
            std::cerr << "Config load failed: " << info.message << std::endl;
            return false;
        }

        // 获取配置快照
        auto cfg = CONFIG_SNAPSHOT;

        // 读取持久化根路径并打开存储
        std::string storeRoot = cfg->getString("common.store.root", "/var/lib/tbox");
        store_ = hwyz::store::Store::open("prov", storeRoot);

        // 通过全局键值读取 VIN
        std::string vin = hwyz::Utils::global_read_string(hwyz::VIN);
        std::cout << "VIN: " << vin << std::endl;

        return true;
    }

    int execute() override {
        auto cfg = CONFIG_SNAPSHOT;

        std::string host = cfg->getString("server.host", "127.0.0.1");
        int port = cfg->getInt("server.port", 8080);
        bool debug = cfg->getBool("log.debug", false);

        std::cout << "Connecting to " << host << ":" << port << std::endl;

        // 使用持久化存储
        int counter = store_.loadOr<int>("counter", 0);
        std::cout << "Counter: " << counter << std::endl;
        store_.save<int>("counter", counter + 1);

        // 主循环
        // ...

        return 0;
    }

    void cleanup() override {
        // 强制刷新持久化数据
        store_.flush();
        std::cout << "ProvService shutting down" << std::endl;
    }

private:
    hwyz::store::Store store_;
};

APPLICATION_ENTRY(ProvService)
```
