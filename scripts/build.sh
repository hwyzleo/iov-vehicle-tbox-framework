#!/bin/bash

# TBOX Framework Build Script

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

# 打印带颜色的消息
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查依赖
check_dependencies() {
    print_info "Checking dependencies..."
    
    # 检查CMake
    if ! command -v cmake &> /dev/null; then
        print_error "CMake is not installed"
        exit 1
    fi
    
    # 检查OpenSSL
    if ! command -v openssl &> /dev/null; then
        print_warn "OpenSSL command not found, but may be available as library"
    fi
    
    print_info "All dependencies are available"
}

# 配置项目
configure_project() {
    print_info "Configuring project..."
    
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    
    cmake "${PROJECT_ROOT}" -DCMAKE_BUILD_TYPE=Release
}

# 构建项目
build_project() {
    print_info "Building project..."
    
    cd "${BUILD_DIR}"
    cmake --build . --config Release -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
}

# 运行测试
run_tests() {
    print_info "Running tests..."
    
    cd "${BUILD_DIR}"
    ctest --output-on-failure --verbose
}

# 安装项目
install_project() {
    print_info "Installing project..."
    
    cd "${BUILD_DIR}"
    cmake --install . --prefix "${PROJECT_ROOT}/install"
}

# 清理构建
clean_build() {
    print_info "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
    rm -rf "${PROJECT_ROOT}/install"
    print_info "Clean completed"
}

# 清理构建产物（保留目录）
clean_objects() {
    print_info "Cleaning build objects..."
    cd "${BUILD_DIR}" 2>/dev/null && make clean 2>/dev/null || true
    print_info "Clean completed"
}

# 显示帮助
show_help() {
    echo "TBOX Framework Build Script"
    echo ""
    echo "Usage: $0 [OPTION]"
    echo ""
    echo "Options:"
    echo "  build       Configure and build the project"
    echo "  test        Build and run tests"
    echo "  clean       Remove build directory"
    echo "  clean-obj   Clean build objects only"
    echo "  install     Install project to ./install"
    echo "  configure   Configure project only"
    echo "  help        Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 build      # Build the project"
    echo "  $0 test       # Build and run tests"
    echo "  $0 clean      # Remove build directory"
    echo "  $0 install    # Install project locally"
}

# 主函数
main() {
    # 检查依赖
    check_dependencies
    
    case "${1:-build}" in
        build)
            configure_project
            build_project
            print_info "Build completed successfully"
            ;;
        test)
            configure_project
            build_project
            run_tests
            print_info "Tests completed successfully"
            ;;
        clean)
            clean_build
            ;;
        clean-obj)
            clean_objects
            ;;
        install)
            configure_project
            build_project
            install_project
            print_info "Install completed successfully"
            ;;
        configure)
            configure_project
            print_info "Configure completed successfully"
            ;;
        help)
            show_help
            ;;
        *)
            print_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
}

# 执行主函数
main "$@"
