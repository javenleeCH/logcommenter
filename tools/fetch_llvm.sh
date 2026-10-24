#!/usr/bin/env bash
# 下载预编译 LLVM/Clang 静态工具链 release asset 并解压到目标目录。
# 用法: tools/fetch_llvm.sh [version] [dest_dir] [build_type]
#   version    LLVM 版本，默认 23.1.3
#   dest_dir   解压目录，默认 .toolchain（debug 时 .toolchain-debug）
#   build_type release|debug，默认 release
# 环境变量: LOGC_LLVM_VERSION / LOGC_LLVM_DEST / LOGC_LLVM_BUILD_TYPE / LOGC_DISABLE_SSL / GH_TOKEN
# 产出: <dest>/lib/cmake/{llvm,clang} 等，供 find_package 复用；
#       debug 另下载 LLVM 源码树到 <dest>/llvm-src（供调试器源码映射）。
set -euo pipefail

VER="${1:-${LOGC_LLVM_VERSION:-23.1.3}}"
BUILD="${3:-${LOGC_LLVM_BUILD_TYPE:-release}}"
case "$BUILD" in
  release) ;;
  debug)   ;;
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

TAG="toolchain-llvm-${VER}"
REPO="${GITHUB_REPOSITORY:-javenleeCH/logcommenter}"
mkdir -p "$DEST"

download_and_extract() {
  local asset="$1" dest="$2"
  local TMP ZIP URL
  TMP="$(mktemp -d)"; ZIP="$TMP/$asset"
  echo "[fetch_llvm] downloading $asset (build=$BUILD) ..."
  # 优先用 gh（GitHub runner 自带，自动处理 release asset 重定向与鉴权）；
  # 否则 curl，并对所有错误（含连接重置）重试。
  if command -v gh >/dev/null 2>&1 && [ -n "${GH_TOKEN:-${GITHUB_TOKEN:-}}" ]; then
    (cd "$TMP" && gh release download "$TAG" --repo "$REPO" --pattern "$asset" --clobber)
  else
    # 单一 SSL 开关：LOGC_DISABLE_SSL=1 时跳过证书校验
    INSECURE=()
    if [ "${LOGC_DISABLE_SSL:-0}" = "1" ]; then INSECURE=(-k); fi
    AUTH=()
    if [ -n "${GH_TOKEN:-${GITHUB_TOKEN:-}}" ]; then
      AUTH=(-H "Authorization: Bearer ${GH_TOKEN:-${GITHUB_TOKEN}}")
    fi
    URL="https://github.com/${REPO}/releases/download/${TAG}/${asset}"
    curl -fL --retry 5 --retry-all-errors --retry-delay 5 --connect-timeout 30 \
      "${INSECURE[@]}" "${AUTH[@]}" "$URL" -o "$ZIP"
  fi
  [ -f "$ZIP" ] || ZIP="$(find "$TMP" -name '*.zip' -print -quit)"
  [ -n "$ZIP" ] && [ -f "$ZIP" ] || { echo "[fetch_llvm] download failed: $asset" >&2; exit 1; }
  echo "[fetch_llvm] downloaded $(du -h "$ZIP" | cut -f1)"
  # Windows: 必须用 PowerShell Expand-Archive（toolchain 用 Compress-Archive 打包，zip 条目
  # 用反斜杠作路径分隔符，Git-bash 的 unzip 不认）。其余平台用 unzip。
  if [ "$PLAT" = "windows" ]; then
    local WINZIP WINDEST
    WINZIP=$(cygpath -w "$ZIP"); WINDEST=$(cygpath -w "$dest")
    powershell -NoProfile -Command "Expand-Archive -LiteralPath '$WINZIP' -DestinationPath '$WINDEST' -Force"
  elif command -v unzip >/dev/null 2>&1; then
    unzip -q "$ZIP" -d "$dest"
  else
    echo "no extraction tool available (need unzip)" >&2; exit 1
  fi
  rm -rf "$TMP"
}

if [ "$BUILD" = "debug" ]; then
  # debug：三份 asset —— libs（.a/.lib+config）、headers（include+clang 内置头）、src（源码树）
  download_and_extract "llvm-clang-${PLAT}-x86_64-${VER}-debug-libs.zip" "$DEST"
  download_and_extract "llvm-clang-${PLAT}-x86_64-${VER}-debug-headers.zip" "$DEST"
  mkdir -p "$DEST/llvm-src"
  download_and_extract "llvm-clang-${VER}-src.zip" "$DEST/llvm-src"
else
  # release：单 asset（库 + 头 + config + clang 内置头）
  download_and_extract "llvm-clang-${PLAT}-x86_64-${VER}.zip" "$DEST"
fi

echo "[fetch_llvm] installed to $DEST"
echo "LLVM_DIR=$DEST/lib/cmake/llvm"
echo "Clang_DIR=$DEST/lib/cmake/clang"
if [ "$BUILD" = "debug" ]; then
  echo "[fetch_llvm] LLVM source tree at: $DEST/llvm-src (for debugger source mapping)"
fi
