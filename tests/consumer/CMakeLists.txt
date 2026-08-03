# tests/consumer/CMakeLists.txt
# TBOX-FW-DSN-CR-010 installed-package consumer.
# 独立项目，仅通过 find_package(TboxFramework CONFIG REQUIRED) 消费已安装的
# framework staging；禁止 add_subdirectory 或引用 framework 源码/私有头。
cmake_minimum_required(VERSION 3.16)
project(tbox_framework_consumer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(TboxFramework CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(framework_consumer src/main.cpp)
target_link_libraries(framework_consumer PRIVATE
    TBoxFramework::hash
    TBoxFramework::config
    TBoxFramework::store
    TBoxFramework::log
    TBoxFramework::ipc
    TBoxFramework::application
    Threads::Threads
)
