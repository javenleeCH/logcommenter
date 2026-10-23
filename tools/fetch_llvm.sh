#!/usr/bin/env bash
# 下载预编译 LLVM/Clang 静态工具链 release asset 并解压到目标目录。
# 用法: tools/fetch_llvm.sh [version] [dest_dir]
# 产出: <dest_dir>/lib/cmake/{llvm,clang} 等，供 find_package 复用。
# 环境变量:
#   LOGC_LLVM_VERSION   默认 23.1.3
#   LOGC_LLVM_DEST      默认 .toolchain
#   GH_TOKEN / GITHUB_TOKEN  可选（私有仓库或提高限额时使用）
set -euo pipefail

VER="${1:-${LOGC_LLVM_VERSION:-23.1.3}}"
DEST="${2:-${LOGC_LLVM_DEST:-.toolchain}}"

case "$(uname -s)" in
  Linux*)  PLAT=linux ;;
  MINGW*|MSYS*|CYGWIN*) PLAT=windows ;;
  Darwin*) PLAT=macos ;;
  *) echo "unsupported platform: $(uname -s)" >&2; exit 1 ;;
esac

ASSET="llvm-clang-${PLAT}-x86_64-${VER}.zip"
TAG="toolchain-llvm-${VER}"
URL="https://github.com/${GITHUB_REPOSITORY:-javenleeCH/logcommenter}/releases/download/${TAG}/${ASSET}"

mkdir -p "$DEST"
if [ -f "$DEST/lib/cmake/llvm/LLVMConfig.cmake" ]; then
  echo "[fetch_llvm] already present at $DEST, skip"
  exit 0
fi

TMP="$(mktemp -d)"
ZIP="$TMP/$ASSET"
echo "[fetch_llvm] downloading $ASSET ..."
if [ -n "${GH_TOKEN:-${GITHUB_TOKEN:-}}" ]; then
  AUTH=(-H "Authorization: Bearer ${GH_TOKEN:-${GITHUB_TOKEN}}")
else
  AUTH=()
fi
curl -fL --retry 3 "${AUTH[@]}" "$URL" -o "$ZIP"

# Windows 上 unzip 可能不可用，优先 unzip，回退 PowerShell
if command -v unzip >/dev/null 2>&1; then
  unzip -q "$ZIP" -d "$DEST"
else
  WINZIP=$(cygpath -w "$ZIP")
  WINDEST=$(cygpath -w "$DEST")
  powershell -Command "Expand-Archive -Path '$WINZIP' -DestinationPath '$WINDEST' -Force"
fi
rm -rf "$TMP"

echo "[fetch_llvm] installed to $DEST"
# 打印 CMake 定位变量，方便直接使用
case "$PLAT" in
  windows) SUBDIR= ;;
  *) SUBDIR= ;;
esac
echo "LLVM_DIR=$DEST/lib/cmake/llvm"
echo "Clang_DIR=$DEST/lib/cmake/clang"
