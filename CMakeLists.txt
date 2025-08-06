project(Framework)
cmake_minimum_required(VERSION 3.10)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 收集源文件
file(GLOB_RECURSE FRAMEWORK_SOURCES src/*.cpp)
file(GLOB_RECURSE FRAMEWORK_HEADERS include/*.h)

# 创建框架库
add_library(hwyz SHARED
        ${FRAMEWORK_SOURCES}
        ${FRAMEWORK_HEADERS}
        )
set_target_properties(hwyz PROPERTIES
        LINKER_LANGUAGE CXX
        )

# 根据不同的环境设置不同的库文件目录
if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    if (APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/arm64-apple-darwin)
    elseif (UNIX AND NOT APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/aarch64-linux-gnu)
    endif ()
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    if (APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/x86_64-apple-darwin)
    elseif (UNIX AND NOT APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/x86_64-linux-gnu)
    endif ()
endif ()

# 添加三方库
if(EXISTS ${LIB_DIR}/yaml-cpp)
    target_link_directories(hwyz PRIVATE ${LIB_DIR}/yaml-cpp)
    target_link_libraries(hwyz PRIVATE yaml-cpp)
else()
    message(WARNING "YAML library directory not found: ${LIB_DIR}/yaml-cpp")
endif()
find_package(OpenSSL REQUIRED)
target_include_directories(hwyz PUBLIC ${OPENSSL_INCLUDE_DIR})
target_link_libraries(hwyz PRIVATE ${OPENSSL_LIBRARIES})

# 设置包含目录
target_include_directories(hwyz
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )

# 配置文件
configure_file(HWYZConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/HWYZConfig.cmake"
        @ONLY)

# 安装配置文件
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/HWYZConfig.cmake"
        DESTINATION lib/cmake/HWYZ
        )

# 配置 pkg-config 文件
configure_file(HWYZ.pc.in
        "${CMAKE_CURRENT_BINARY_DIR}/HWYZ.pc"
        @ONLY)

# 安装 pkg-config 文件
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/HWYZ.pc"
        DESTINATION lib/pkgconfig
        )

# 安装规则
install(TARGETS hwyz
        EXPORT HWYZ-targets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
        )

install(DIRECTORY include/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h"
        )

# 导出配置
install(EXPORT HWYZ-targets
        FILE HWYZTargets.cmake
        NAMESPACE HWYZ::
        DESTINATION lib/cmake/HWYZ
        )