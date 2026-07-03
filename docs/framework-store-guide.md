# Framework Store 使用指南

## 概述

`framework-store` 是 TBOX 框架的本地持久化组件，提供原子、掉电安全的本地写盘与类型化读写能力。

## 主要特性

- **原子写入**: temp → fsync → rename → fsync(dir) 模式
- **掉电安全**: 任意时刻掉电不产生损坏/半截文件
- **并发保护**: 多进程/多线程文件锁
- **类型化 API**: 支持 int, double, bool, std::string
- **降写策略**: 脏标记 + 去抖/批量 flush

## 快速开始

### 1. 包含头文件

```cpp
#include "store.h"
using namespace hwyz::store;
```

### 2. 打开存储

```cpp
// 打开存储（绑定到 /var/lib/tbox/prov/）
Store store = Store::open("prov");

// 或指定自定义根目录
Store store = Store::open("prov", "/custom/path");
```

### 3. 保存数据

```cpp
// 保存整数
store.save<int>("counter", 42);

// 保存字符串
store.save<std::string>("session.id", "abc123");

// 保存布尔值
store.save<bool>("feature.enabled", true);
```

### 4. 加载数据

```cpp
// 加载整数（缺失时抛异常）
int counter = store.load<int>("counter");

// 加载整数（缺失时返回默认值）
int counter = store.loadOr<int>("counter", 0);

// 检查key是否存在
if (store.has("counter")) {
    // ...
}
```

### 5. 删除数据

```cpp
store.remove("counter");
```

### 6. 强制刷新

```cpp
// 在去抖模式下强制落盘
store.flush();
```

## 错误处理

```cpp
try {
    store.save<int>("key", 42);
} catch (const StoreException& e) {
    StoreErrorInfo error = e.getError();
    std::cerr << "Error: " << error.message << std::endl;

    switch (error.code) {
        case StoreError::kPathUnavailable:
            // 存储路径不可用
            break;
        case StoreError::kAtomicWriteFailed:
            // 原子写失败
            break;
        case StoreError::kLockFailed:
            // 并发锁获取失败
            break;
        case StoreError::kSerializationFailed:
            // 序列化失败
            break;
        case StoreError::kKeyNotFound:
            // key不存在
            break;
    }
}
```

## 存储路径

数据存储在 `/var/lib/tbox/<svc>/<key>.dat`

- 目录权限: 0700
- 文件权限: 0600

## 并发安全

- 多进程: 使用 flock 文件锁
- 多线程: 使用 std::mutex

## 掉电安全

每次写入都会执行 fsync，确保数据落盘。

## 性能优化

- 去抖模式: 批量提交减少写入次数
- 脏标记: 只刷新有变化的数据

## 最佳实践

1. 在服务启动时打开存储
2. 使用有意义的key命名（如 `session.id`, `counter.total`）
3. 处理所有可能的异常
4. 在服务关闭前调用 flush()
