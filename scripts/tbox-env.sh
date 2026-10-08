#!/bin/bash
#
# TBOX 构建环境的单一来源（single source of truth）
#
# framework 与所有 TBOX 服务的 build.sh 都 source 本文件，用于确定：
#   - framework 的安装前缀（TBOX_PREFIX）
#   - 消费方查找 TBoxFramework 包的路径（TBoxFramework_DIR）
#
# 用法（在各项目 scripts/build.sh 中）：
#   source "$(dirname "${BASH_SOURCE[0]}")/../../iov-vehicle-tbox-framework/scripts/tbox-env.sh"
#
# 切换环境不需要改任何文件，只需覆盖环境变量：
#   本地开发（默认）：  ./scripts/build.sh
#   生产/CI：           TBOX_PREFIX=/opt/tbox ./scripts/build.sh
#   交叉编译 staging：  TBOX_PREFIX=/path/to/sysroot/usr ./scripts/build.sh
#
# 注意：本文件只做变量赋值，不得有副作用（不要在此编译/安装/打印大段日志），
#       因为它会被所有项目在 configure 之前 source。

# framework 的安装前缀。已存在的环境变量优先，便于 CI/生产覆盖。
: "${TBOX_PREFIX:=${HOME}/.local}"
export TBOX_PREFIX

# 消费方定位 TboxFrameworkConfig.cmake 的目录。
#
# 注意大小写：package 名是 TboxFramework（安装文件 TboxFrameworkConfig.cmake，
# 目录 lib/cmake/TboxFramework），只有 target 命名空间才是 TBoxFramework::。
# CMake 的 <PackageName>_DIR 变量名大小写敏感（与文件系统敏感性无关），
# 故 find_package(TboxFramework) 只认 TboxFramework_DIR。
export TboxFramework_DIR="${TBOX_PREFIX}/lib/cmake/TboxFramework"

# 兼容别名：历史上各服务 scripts/build.sh 传 -DTBoxFramework_DIR="${TBoxFramework_DIR}"
# （大写 B，CMake 其实不会读取）。保留导出，避免这些脚本传入空值。
# 新增脚本请统一使用 TboxFramework_DIR 或 CMAKE_PREFIX_PATH。
export TBoxFramework_DIR="${TboxFramework_DIR}"

# CMAKE_PREFIX_PATH 通用入口：framework 与各服务 client SDK（TboxProvClient /
# TboxSecClient / ...）都装在同一前缀下，一个入口即可解析全部 package。
# 幂等：本文件会被多个 build.sh source，已包含则不重复追加。
case ":${CMAKE_PREFIX_PATH:-}:" in
    *":${TBOX_PREFIX}:"*) ;;
    "::") export CMAKE_PREFIX_PATH="${TBOX_PREFIX}" ;;
    *)    export CMAKE_PREFIX_PATH="${TBOX_PREFIX}:${CMAKE_PREFIX_PATH}" ;;
esac

# framework 源码树位置（供仍需引用源码/第三方库的场景使用）
TBOX_ENV_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export TBOX_FRAMEWORK_ROOT="$(cd "${TBOX_ENV_SCRIPT_DIR}/.." && pwd)"
unset TBOX_ENV_SCRIPT_DIR
