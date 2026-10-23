#!/usr/bin/env bash
# 下载预编译 LLVM/Clang 静态工具链 release asset 并解压到目标目录。
# 用法: tools/fetch_llvm.sh [version] [dest_dir] [build_type]
#   version    LLVM 版本，默认 23.1.3
#   dest_dir   解压目录，默认 .toolchain（debug 时 .toolchain-debug）
#   build_type release|debug，默认 release（debug 下 asset 带 -debug 后缀，含 -g 符号）
# 环境变量: LOGC_LLVM_VERSION / LOGC_LLVM_DEST / LOGC_LLVM_BUILD_TYPE / LOGC_DISABLE_SSL / GH_TOKEN
# 产出: <dest>/lib/cmake/{llvm,clang} 等，供 find_package 复用。
set -euo pipefail

VER="${1:-${LOGC_LLVM_VERSION:-23.1.3}}"
BUILD="${3:-${LOGC_LLVM_BUILD_TYPE:-release}}"
case "$BUILD" in
  release) SUFFIX="" ;;
  debug)   SUFFIX="-debug" ;;
  *) echo "unknown build_type: $BUILD (use release|debug)" >&2; exit 1 ;;
esac
DEST="${2:-${LOGC_LLVM_DEST:-}}"
if [ -z "$DEST" ]; then
  [ "$BUILD" = "debug" ] && DEST=".toolchain-debug" || DEST=".toolchain"
fi

case "$(uname -s)" in
  Linux*)  PLAT=linux ;;
  MINGW*|MSYS*|CYGWIN*) PLAT=windows ;;
  Darwin*) PLAT=macos ;;
  *) echo "unsupported platform: $(uname -s)" >&2; exit 1 ;;
esac

ASSET="llvm-clang-${PLAT}-x86_64-${VER}${SUFFIX}.zip"
TAG="toolchain-llvm-${VER}"
URL="https://github.com/${GITHUB_REPOSITORY:-javenleeCH/logcommenter}/releases/download/${TAG}/${ASSET}"

mkdir -p "$DEST"
if [ -f "$DEST/lib/cmake/llvm/LLVMConfig.cmake" ]; then
  echo "[fetch_llvm] already present at $DEST, skip"
  exit 0
fi

TMP="$(mktemp -d)"
ZIP="$TMP/$ASSET"
echo "[fetch_llvm] downloading $ASSET (build=$BUILD) ..."
REPO="${GITHUB_REPOSITORY:-javenleeCH/logcommenter}"

# 优先用 gh（GitHub runner 自带，自动处理 release asset 重定向与鉴权）；
# 否则 curl，并对所有错误（含连接重置）重试。
if command -v gh >/dev/null 2>&1 && [ -n "${GH_TOKEN:-${GITHUB_TOKEN:-}}" ]; then
  (cd "$TMP" && gh release download "$TAG" --repo "$REPO" --pattern "$ASSET" --clobber)
else
  # 单一 SSL 开关：LOGC_DISABLE_SSL=1 时跳过证书校验
  INSECURE=()
  if [ "${LOGC_DISABLE_SSL:-0}" = "1" ]; then INSECURE=(-k); fi
  if [ -n "${GH_TOKEN:-${GITHUB_TOKEN:-}}" ]; then
    AUTH=(-H "Authorization: Bearer ${GH_TOKEN:-${GITHUB_TOKEN}}")
  else
    AUTH=()
  fi
  curl -fL --retry 5 --retry-all-errors --retry-delay 5 --connect-timeout 30 \
    "${INSECURE[@]}" "${AUTH[@]}" "$URL" -o "$ZIP"
fi

# 统一找到下载好的 zip（gh 下载到 TMP，curl 直接写 ZIP）
[ -f "$ZIP" ] || ZIP="$(find "$TMP" -name '*.zip' -print -quit)"
[ -n "$ZIP" ] && [ -f "$ZIP" ] || { echo "[fetch_llvm] download failed: $ASSET" >&2; exit 1; }
echo "[fetch_llvm] downloaded $(du -h "$ZIP" | cut -f1)"

# Windows: 必须用 PowerShell Expand-Archive（toolchain 用 Compress-Archive 打包，zip 条目
# 用反斜杠作路径分隔符，Git-bash 的 unzip 不认）。其余平台用 unzip。
if [ "$PLAT" = "windows" ]; then
  WINZIP=$(cygpath -w "$ZIP")
  WINDEST=$(cygpath -w "$DEST")
  powershell -NoProfile -Command "Expand-Archive -LiteralPath '$WINZIP' -DestinationPath '$WINDEST' -Force"
elif command -v unzip >/dev/null 2>&1; then
  unzip -q "$ZIP" -d "$DEST"
else
  echo "no extraction tool available (need unzip)" >&2; exit 1
fi
rm -rf "$TMP"

echo "[fetch_llvm] installed to $DEST"
echo "LLVM_DIR=$DEST/lib/cmake/llvm"
echo "Clang_DIR=$DEST/lib/cmake/clang"
