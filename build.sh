#!/usr/bin/env bash
# 一键 configure + build + test（Ninja，Linux/macOS/WSL 通用）。
# 用法: ./build.sh [debug|release]
# 自动启用 ccache（已安装时），加速 LLVM 增量构建；未安装则跳过。
set -euo pipefail
cd "$(dirname "$0")"

MODE="${1:-debug}"
case "$MODE" in
  release) BUILD_PRESET=ninja-release ;;
  *)       BUILD_PRESET=ninja-debug ;;
esac

# 代理：CPM 拉取 llvm-project 需要（默认 127.0.0.1:4780，可用环境变量覆盖）
[ -z "${http_proxy:-}" ]  && export http_proxy="http://127.0.0.1:4780"
[ -z "${https_proxy:-}" ] && export https_proxy="http://127.0.0.1:4780"

CACHE_ARGS=()
if command -v ccache >/dev/null 2>&1; then
  CACHE_ARGS+=("-DCMAKE_CXX_COMPILER_LAUNCHER=ccache")
  echo "[build] ccache detected, enabled."
fi

cmake --preset ninja "${CACHE_ARGS[@]}"
cmake --build --preset "$BUILD_PRESET"
ctest --test-dir out/build/ninja --output-on-failure
