#!/usr/bin/env bash
# 一键 configure + build + test（Ninja，Linux/macOS/WSL 通用）。
# 用法: ./build.sh [debug|release] [compiler]
#   compiler: auto(默认，用环境 CC/CXX 或系统默认) | msvc | clang-cl | clang
# 自动启用 ccache（已安装时），加速 LLVM 增量构建；未安装则跳过。
# 网络代理请通过环境变量 http_proxy/https_proxy 自行设置，脚本不预设。
set -euo pipefail
cd "$(dirname "$0")"

MODE="${1:-debug}"
COMPILER="${2:-auto}"
case "$MODE" in
  release) BUILD_SUFFIX=release ;;
  *)       BUILD_SUFFIX=debug ;;
esac
case "$COMPILER" in
  auto)      PRESET=ninja ;;
  msvc)      PRESET=ninja-msvc ;;
  clang-cl)  PRESET=ninja-clang-cl ;;
  clang)     PRESET=ninja-clang ;;
  *) echo "unknown compiler: $COMPILER (auto|msvc|clang-cl|clang)"; exit 1 ;;
esac

CACHE_ARGS=()
if command -v ccache >/dev/null 2>&1; then
  CACHE_ARGS+=("-DCMAKE_CXX_COMPILER_LAUNCHER=ccache")
  echo "[build] ccache detected, enabled."
fi

cmake --preset "$PRESET" "${CACHE_ARGS[@]}"
cmake --build --preset "${PRESET}-${BUILD_SUFFIX}"
ctest --test-dir "out/build/${PRESET}" --output-on-failure
