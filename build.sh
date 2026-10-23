#!/usr/bin/env bash
# 一键 fetch 预编译 LLVM + configure + build + test（Ninja，Linux/macOS/WSL 通用）。
# 使用 unix-default preset（系统默认 GCC/Clang）；Windows 请用 build.bat（windows-msvc）。
# 本机已安装 LLVM 时，可直接 export LLVM_DIR/Clang_DIR 跳过下载。
set -euo pipefail
cd "$(dirname "$0")"

PRESET=unix-default
if [ -z "${LLVM_DIR:-}" ]; then
  bash tools/fetch_llvm.sh
  LLVM_DIR="$PWD/.toolchain/lib/cmake/llvm"
  Clang_DIR="$PWD/.toolchain/lib/cmake/clang"
fi

cmake --preset "$PRESET" -DLLVM_DIR="$LLVM_DIR" -DClang_DIR="$Clang_DIR"
cmake --build "build/$PRESET"
ctest --test-dir "build/$PRESET" --output-on-failure
