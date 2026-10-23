#!/usr/bin/env bash
# 一键 fetch 预编译 LLVM + configure + build + test（Ninja，Linux/macOS/WSL 通用）。
# 用法: ./build.sh [release|debug]
#   release（默认）：下载 Release 版 LLVM（.toolchain），用 unix-default preset（Release）编译；
#   debug：下载带 -g 符号的 Debug 版 LLVM（.toolchain-debug），用 unix-default-debug preset（Debug）
#          编译，产物在 build/unix-default-debug/，可单步进入 LLVM/Clang 库内部调试。
# 本机已装 LLVM 时，可直接 export LLVM_DIR/Clang_DIR 跳过下载。
set -euo pipefail
cd "$(dirname "$0")"

MODE="${1:-release}"
case "$MODE" in
  release) PRESET="unix-default";         TC=".toolchain";        TCARG="" ;;
  debug)   PRESET="unix-default-debug";   TC=".toolchain-debug";  TCARG="debug" ;;
  *) echo "usage: ./build.sh [release|debug]" >&2; exit 1 ;;
esac

if [ -z "${LLVM_DIR:-}" ]; then
  bash tools/fetch_llvm.sh "" "$TC" "$TCARG"
  LLVM_DIR="$PWD/$TC/lib/cmake/llvm"
  Clang_DIR="$PWD/$TC/lib/cmake/clang"
fi

cmake --preset "$PRESET" -DLLVM_DIR="$LLVM_DIR" -DClang_DIR="$Clang_DIR"
cmake --build "build/$PRESET"
ctest --test-dir "build/$PRESET" --output-on-failure
