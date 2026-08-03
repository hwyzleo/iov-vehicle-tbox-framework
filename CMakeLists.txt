# TBoxFramework - TBOX 基础框架公共库
#
# TBOX-FW-DSN-CR-010: 组件化 CMake、GNUInstallDirs、install/export 与 SDK 契约。
# 承接 TBOX-BUILD-DSN-CR-002 接口契约：
#   - 6 个独立组件 target: framework-{config,store,log,ipc,hash,application}
#   - install components: framework-sdk (头文件/静态库/CMake package)
#                        framework-runtime (运行时动态库；本期静态优先，留空)
#   - yaml-cpp 由外部 find_package 提供 (BUILD: TBOX_DEP_STAGING; host: system)
#   - spdlog 由 framework 自包含 (header-only, 随 framework-sdk 安装)
#   - OpenSSL/pthread 来自 sysroot (Orin) 或 system (host)
#
# framework 不设置 compiler/sysroot；由 BUILD toolchain/preset 注入。

cmake_minimum_required(VERSION 3.16)
project(TboxFramework VERSION 0.1.0 LANGUAGES C CXX)

# --- C++ 标准（toolchain 可能已设置；不覆盖 Orin preset） ---
if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()
if(NOT CMAKE_C_STANDARD)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(TBOX_FRAMEWORK_BUILD_TESTING "Build framework unit tests" ON)

# ===========================================================================
# 第三方依赖
# ===========================================================================

# --- spdlog: framework 自包含 header-only (含 bundled fmt)，随 SDK 安装 ---
# 设计明确 spdlog 不进 BUILD lock，由 framework 自包含。
add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/tbox-framework>
)
set_target_properties(spdlog PROPERTIES EXPORT_NAME spdlog)

# --- yaml-cpp: external (BUILD: TBOX_DEP_STAGING; host: system/homebrew) ---
# 不再使用 third_party 下的预编译库（arch 专属，Orin 不可用）。
find_package(yaml-cpp CONFIG REQUIRED)

# --- OpenSSL: PRIVATE (仅 utils.cpp AES 解密；sysroot 或 system) ---
find_package(OpenSSL REQUIRED)

# --- Threads: pthread (sysroot 或 system) ---
find_package(Threads REQUIRED)

# ===========================================================================
# 组件公共设定
# ===========================================================================
# 所有组件 PUBLIC 暴露 include/；静态库优先，启用 PIC 以便下游链入 .so。
function(tbox_framework_component _target)
    set_target_properties(${_target} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(${_target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/tbox-framework>
    )
endfunction()

# ===========================================================================
# framework-hash (无依赖；FIPS 180-4 SHA-256)
# ===========================================================================
add_library(framework-hash STATIC
    src/hash/sha256.cpp
)
tbox_framework_component(framework-hash)
# CR-006: hash 源文件启用 -Wall -Wextra -Werror
target_compile_options(framework-hash PRIVATE -Wall -Wextra -Werror)
set_target_properties(framework-hash PROPERTIES EXPORT_NAME hash)

# ===========================================================================
# framework-config (PUBLIC yaml-cpp；配置加载/深合并/校验/不可变快照)
# ===========================================================================
add_library(framework-config STATIC
    src/config/config_manager.cpp
    src/config/config_merger.cpp
    src/config/config_validator.cpp
    src/config/immutable_config_view.cpp
    src/config/path_resolver.cpp
)
tbox_framework_component(framework-config)
target_include_directories(framework-config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(framework-config PUBLIC yaml-cpp::yaml-cpp)
set_target_properties(framework-config PROPERTIES EXPORT_NAME config)

# ===========================================================================
# framework-store (PRIVATE yaml-cpp/Threads；原子写/文件锁/降写)
# ===========================================================================
add_library(framework-store STATIC
    src/store/store.cpp
    src/store/store_atomic_writer.cpp
    src/store/store_file_lock.cpp
    src/store/store_flush_policy.cpp
    src/store/store_path_resolver.cpp
    src/store/store_serializer.cpp
)
tbox_framework_component(framework-store)
target_include_directories(framework-store PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(framework-store PRIVATE yaml-cpp::yaml-cpp Threads::Threads)
set_target_properties(framework-store PROPERTIES EXPORT_NAME store)

# ===========================================================================
# framework-log (PUBLIC spdlog；PRIVATE yaml-cpp/Threads；统一日志)
# ===========================================================================
add_library(framework-log STATIC
    src/log/log_async_dispatcher.cpp
    src/log/log_config_adapter.cpp
    src/log/log_console_sink.cpp
    src/log/log_emergency_writer.cpp
    src/log/log_enricher.cpp
    src/log/log_json_formatter.cpp
    src/log/log_level_filter.cpp
    src/log/log_logger.cpp
    src/log/log_redactor.cpp
    src/log/log_rolling_file_sink.cpp
    src/log/log_sink_manager.cpp
    src/log/log_spdlog_adapter.cpp
    src/log/log_standard_formatter.cpp
    src/log/log_types.cpp
)
tbox_framework_component(framework-log)
target_include_directories(framework-log PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(framework-log
    PUBLIC spdlog
    PRIVATE yaml-cpp::yaml-cpp Threads::Threads)
set_target_properties(framework-log PROPERTIES EXPORT_NAME log)

# ===========================================================================
# framework-ipc (PRIVATE framework-log/Threads；Unix socket IPC)
# ===========================================================================
add_library(framework-ipc STATIC
    src/ipc/ipc_client.cpp
    src/ipc/ipc_log_adapter.cpp
    src/ipc/ipc_protocol.cpp
    src/ipc/ipc_server.cpp
)
tbox_framework_component(framework-ipc)
target_include_directories(framework-ipc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(framework-ipc PRIVATE framework-log Threads::Threads)
set_target_properties(framework-ipc PROPERTIES EXPORT_NAME ipc)

# ===========================================================================
# framework-application
# (PUBLIC framework-config/framework-log/yaml-cpp/spdlog；PRIVATE OpenSSL/Threads)
# 含 src/utils.cpp (hwyz::Utils, AES/file/base64)，折入 application 因共享 hwyz::
# 命名空间且无内部调用者；application.cpp 直接 include log 私有头，故 PRIVATE src。
# ===========================================================================
add_library(framework-application STATIC
    src/application.cpp
    src/utils.cpp
)
tbox_framework_component(framework-application)
target_include_directories(framework-application PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(framework-application
    PUBLIC framework-config framework-log yaml-cpp::yaml-cpp spdlog
    PRIVATE OpenSSL::Crypto Threads::Threads)
set_target_properties(framework-application PROPERTIES EXPORT_NAME application)

# ===========================================================================
# 内部便利 target（仅测试用，不安装）：聚合全部组件
# ===========================================================================
add_library(framework-all INTERFACE)
target_link_libraries(framework-all INTERFACE
    framework-hash framework-config framework-store
    framework-log framework-ipc framework-application)

# ===========================================================================
# 安装：组件静态库 + 公共头 + spdlog 头 + CMake package -> framework-sdk
# ===========================================================================

# 组件静态库 + spdlog 导出
install(TARGETS
        framework-hash framework-config framework-store
        framework-log framework-ipc framework-application
        spdlog
    EXPORT TboxFrameworkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT framework-sdk
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT framework-sdk
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT framework-sdk
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tbox-framework
)

# 公共头文件
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tbox-framework
    COMPONENT framework-sdk
    FILES_MATCHING PATTERN "*.h"
)

# spdlog header-only（自包含，随 SDK 安装；消费方无需自备 spdlog）
install(DIRECTORY third_party/include/spdlog
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tbox-framework
    COMPONENT framework-sdk
    FILES_MATCHING PATTERN "*.h"
)

# framework-runtime: 本期组件纯静态，无运行时 .so；component 存在但安装为空，
# 符合设计「静态链接组件不重复进入 rootfs」。未来引入动态库时填充。

# --- CMake package ---
set(TBOX_FRAMEWORK_CMAKE_CONFIG_DEST
    ${CMAKE_INSTALL_LIBDIR}/cmake/TboxFramework)

# 配置版本文件
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/TboxFrameworkConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# 配置文件（可重定位：不固化 CMAKE_INSTALL_PREFIX）
configure_file(cmake/TboxFrameworkConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/TboxFrameworkConfig.cmake"
    @ONLY)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/TboxFrameworkConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/TboxFrameworkConfigVersion.cmake"
    DESTINATION ${TBOX_FRAMEWORK_CMAKE_CONFIG_DEST}
    COMPONENT framework-sdk
)

install(EXPORT TboxFrameworkTargets
    FILE TboxFrameworkTargets.cmake
    NAMESPACE TBoxFramework::
    DESTINATION ${TBOX_FRAMEWORK_CMAKE_CONFIG_DEST}
    COMPONENT framework-sdk
)

# ===========================================================================
# 单元测试（HOST 优先；BUILD_TESTING 控制）
# ===========================================================================
if(TBOX_FRAMEWORK_BUILD_TESTING)
    enable_testing()

    set(TEST_SOURCES
        tests/test_config_manager.cpp
        tests/test_path_resolver.cpp
        tests/test_store_path_resolver.cpp
        tests/test_store_serializer.cpp
        tests/test_store_atomic_writer.cpp
        tests/test_config_merger.cpp
        tests/test_config_validator.cpp
        tests/test_store_file_lock.cpp
        tests/test_store_flush_policy.cpp
        tests/test_store_manager.cpp
        tests/test_store_integration.cpp
        tests/test_store_configurable_path.cpp
        tests/test_log_config_adapter.cpp
        tests/test_log_enricher.cpp
        tests/test_log_redactor.cpp
        tests/test_log_level_filter.cpp
        tests/test_log_json_formatter.cpp
        tests/test_log_async_dispatcher.cpp
        tests/test_log_context_scope.cpp
        tests/test_log_integration.cpp
        tests/test_log_standard_formatter.cpp
        tests/test_ipc_protocol.cpp
        tests/test_ipc_server.cpp
        tests/test_ipc_client.cpp
        tests/test_ipc_integration.cpp
        tests/test_ipc_compatibility.cpp
        tests/test_sha256.cpp
        tests/test_sha256_cross_platform.cpp
        tests/test_application_log.cpp
        tests/test_application_signals.cpp
        tests/test_application_shutdown.cpp
        tests/test_application_integration.cpp
        )

    foreach(TEST_SOURCE ${TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
        add_executable(${TEST_NAME} ${TEST_SOURCE})
        target_link_libraries(${TEST_NAME} PRIVATE framework-all)
        # 测试可能 include 组件私有头（src/ 下），保留 src 作为 PRIVATE include
        target_include_directories(${TEST_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
        # CR-006: hash 测试启用 -Wall -Wextra -Werror
        if(${TEST_NAME} MATCHES "sha256")
            target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Werror)
        endif()
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endforeach()
endif()
