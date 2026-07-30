# Application 迁移指南 (TBOX-FW-DSN-CR-007)

本指南面向将 TBOX 服务迁移到重构后的 `hwyz::Application` 基类。CR-007 将 Application
统一接入 framework-log、提供可定制且 async-signal-safe 的信号处理，并为长驻主循环提供
退出查询/请求/等待能力。framework-ipc 继续保持独立可组合，不进入基类。

## 1. 配置字段映射

Application 现仅读取 framework-log 的唯一 schema `common.log.*`，旧 `type`/`path` 不再生效。

| 旧字段 | 新字段 | 说明 |
| --- | --- | --- |
| `common.log.type: file` | `common.log.file.enabled: true` | 文件 sink 开关 |
| `common.log.type: console`（或缺省） | `common.log.console.enabled: true` | console sink 开关 |
| `common.log.path: /x/y.log` | `common.log.file.root: /var/log/tbox` | 文件 sink 根目录，实际路径 `<root>/<svc>/<svc>_0.log` |
| `common.log.max_size_mb` | `common.log.file.max_file_size_mb` | 单文件大小上限 |
| `common.log.max_files` | `common.log.file.max_files` | 滚动文件数 |
| （无） | `common.log.file.total_budget_mb` | 总空间预算，须 `max_file_size_mb × max_files ≤ total_budget_mb` |
| （无） | `common.log.schema_version: 1` | 必填，本期只接受 1 |
| （无） | `common.log.level: INFO` | 全局阈值 |
| （无） | `common.log.strict: false` | 严格模式：初始化失败 fail-closed |
| （无） | `common.log.format: standard` | `standard` 或 `json` |
| （无） | `common.log.async.enabled: true` | 异步队列开关 |
| （无） | `common.log.redact.identifiers: mask` | 脱敏策略 |

迁移后 `common.yaml` 示例：

```yaml
common:
  log:
    schema_version: 1
    level: INFO
    format: json
    strict: false
    console:
      enabled: true
    file:
      enabled: false
      root: /var/log/tbox
      max_file_size_mb: 20
      max_files: 5
      total_budget_mb: 100
    async:
      enabled: true
      queue_size: 4096
      flush_interval_ms: 1000
```

服务级覆盖（`<svc>.log.*`）仅允许覆盖 `level`、`format` 与模块级别：

```yaml
prov:
  log:
    level: INFO
    modules:
      transport: WARN
```

> 未知旧字段在严格模式下按 framework-log 规则失败；非严格模式产生可诊断告警。

## 2. Application 子类示例（守护进程）

```cpp
#include "application.h"
#include "ipc.h"  // framework-ipc，在子类内组合，不进入基类

class ProvApplication final : public hwyz::Application {
protected:
    // 可定制 graceful 信号（如加入 SIGHUP）
    std::vector<int> gracefulSignals() const override {
        return {SIGINT, SIGTERM, SIGHUP};
    }

    std::string getServiceName() const override { return "prov"; }

    bool initialize() override {
        return ipc_server_.start(/* request_handler, disconnect_handler */);
    }

    int execute() override {
        // 阻塞等待停机；graceful 信号或 requestShutdown() 唤醒
        waitForShutdown();
        return 0;
    }

    void cleanup() override {
        // 幂等停机：停止接收 -> 中断等待 -> 关闭 fd -> join 线程 -> 释放资源
        ipc_server_.stop();
    }

private:
    tbox::fw::ipc::Server ipc_server_;
};

APPLICATION_ENTRY(ProvApplication)
```

### 关键行为变化

- **execute() 改为协作式**：基类直接调用 `execute()`，不再以独立线程 + 1s 轮询托管。
  长驻服务应在 `execute()` 内调用 `waitForShutdown()` 或轮询 `isShutdownRequested()`。
  依赖 `EINTR` 退出循环的旧代码须改为查询 `isShutdownRequested()`。
- **日志统一**：`setup_logging()` 默认调用 framework-log，并安装 spdlog 兼容 adapter。
  服务**不得**再次 `spdlog::set_default_logger()` 或自行创建 spdlog sink。
  遗留 `spdlog::xxx` 调用会经 adapter 进入同一 sink，无需逐行改造即可运行。
- **SIGPIPE 默认忽略**：IPC 对端断开时写 socket 不再终止进程。
- **graceful 信号启用 SA_RESTART**：被信号中断的系统调用自动恢复。

## 3. 新增 protected API 速查

| 方法 | 默认行为 | 说明 |
| --- | --- | --- |
| `useFrameworkLog()` | `true` | 短期兼容/测试可覆盖为 false；生产不得恢复第二套日志 |
| `buildLogConfig()` | 从已加载配置解析 | 子类可覆盖以注入测试配置 |
| `gracefulSignals()` | `{SIGINT, SIGTERM}` | 返回集合须去重且与其他集合互斥 |
| `fatalSignals()` | `{SIGSEGV, SIGABRT}` | 安装时带 `SA_RESETHAND` |
| `ignoredSignals()` | `{SIGPIPE}` | 安装为 `SIG_IGN` |
| `isShutdownRequested()` | — | noexcept，跨线程无数据竞争 |
| `requestShutdown()` | — | noexcept，幂等 |
| `waitForShutdown(poll=100ms)` | — | sleep-poll，不忙等 |
| `validateSignalSets(...)` | — | 静态校验工具，返回空串=通过 |

> `getConfig()` 本期保留 `[[deprecated]]`，阶段 2/3 迁移清零后另立 breaking CR 删除。

## 4. 逐服务迁移检查表

建议顺序：sec -> prov -> mqtt -> someip -> diag -> mcu -> rvc -> rsms，每个服务独立变更、独立验证。

- [ ] 配置：`common.log.*` 已改为唯一 schema；旧 `type`/`path` 移除；strict/非 strict 行为符合预期。
- [ ] 日志：删除服务内重复的 `LogAdapter::init()` / `spdlog::set_default_logger()`；启动至退出只有统一结构化日志，无裸 `std::cout`、无重复 sink/行。
- [ ] 信号：所需信号可由 `gracefulSignals()/fatalSignals()/ignoredSignals()` 表达（PROV 覆盖 SIGHUP）；SIGINT/SIGTERM 触发 graceful 停机；SIGPIPE 不终止进程。
- [ ] 主循环：`execute()` 通过 `waitForShutdown()` 或 `isShutdownRequested()` 响应停机；无依赖 `EINTR` 退出的旧循环。
- [ ] IPC：在 `initialize()/execute()/cleanup()` 内组合 framework-ipc，不向 Application 增加 IPC 依赖。
- [ ] cleanup：幂等；停止接收 -> 中断等待 -> 关闭 fd -> join 线程 -> 释放资源；恰好调用一次。
- [ ] 构建：使用新头文件重新构建；无 `getConfig()` deprecation warning（阶段 2 起）。
- [ ] 验证：构建、启动、IPC 回环、断链 SIGPIPE、graceful 停机、异常启动、资源释放、日志格式全部通过。
- [ ] 观察期：单服务无回归后再进入下一服务。

## 5. 共享 dylib 版本化与回滚

### ABI 注意

新增虚函数改变 `Application` 的 vtable 布局，**不保证**对已编译子类二进制兼容：

- 新 `libtbox-framework.dylib` **不得**与基于旧头文件编译的 tsp/mcu/rvc/rsms 混装。
- 安装新 dylib 前，所有继承 Application 的服务须用同一提交/头文件重新构建并完成链接、启动与停机验证。
- 仅动态加载该库但不继承 Application 的服务也需启动冒烟测试。
- 发布采用兼容版本/soname 或版本目录；不得无版本原地覆盖后直接全量运行。

### 阶段 1（框架离线实现）

- 新 dylib 输出到隔离构建目录（如 `build/`），**不覆盖** `install/` 副本，不改变任何运行服务。
- framework 全量单测通过；旧服务源码用新头文件重编通过。
- 无需运行时回滚（未切换运行环境）。

### 阶段 2/3 回滚

发布前须保存：

- 旧 `libtbox-framework.dylib` 完整副本、版本/校验和、头文件/构建提交。
- 与旧 dylib 匹配的 tsp/mcu/rvc/rsms 可执行文件。
- 新旧配置文件快照（尤其 `common.log.*`）。
- 服务启动顺序、健康检查与日志检查命令。

回滚触发条件：进程启动失败、异常信号退出、日志无法解析/丢失/重复、graceful 停机超时、IPC 回归、ABI 异常。

回滚步骤：

1. 停止受影响服务（禁止在进程运行中替换 dylib）。
2. 原子恢复旧 dylib/版本链接，**并**恢复匹配的旧服务二进制；不得只回滚其中一项。
3. 恢复旧配置快照。
4. 按依赖顺序启动服务，执行动态库路径、启动日志、IPC、graceful 停机检查。
5. 阶段 3 单服务迁移回归时，优先回滚该服务及其匹配制品；共享 dylib 已切换则按同一发布单元回滚所有 Application 消费方。
