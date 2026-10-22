#!/usr/bin/env bash
# 一键 configure + build + test（Ninja，Linux/macOS/WSL 通用）。
# 使用 unix-default preset（系统默认 GCC/Clang）；Windows 请用 build.bat（windows-msvc）。
set -euo pipefail
cd "$(dirname "$0")"

PRESET=unix-default
CACHE_ARGS=()
if command -v ccache >/dev/null 2>&1; then
  CACHE_ARGS+=("-DCMAKE_CXX_COMPILER_LAUNCHER=ccache")
  echo "[build] ccache detected, enabled."
fi

cmake --preset "$PRESET" "${CACHE_ARGS[@]}"
cmake --build "build/$PRESET"
ctest --test-dir "build/$PRESET" --output-on-failure
