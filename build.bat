@echo off
REM 一键 configure + build + test（Ninja，Windows）。
REM 用法: build.bat [debug|release]
REM 编译器走环境变量 CC/CXX（默认 cl.exe，须已加入 PATH 且 INCLUDE/LIB 可用，
REM 例如 scoop 安装的 msvc-kit）。自动启用 ccache（已安装时），未安装则跳过。
setlocal
cd /d "%~dp0"

set MODE=%1
if "%MODE%"=="" set MODE=debug
if /I "%MODE%"=="release" (
  set BUILD_PRESET=ninja-release
) else (
  set BUILD_PRESET=ninja-debug
)

if not defined CC set CC=cl.exe
if not defined CXX set CXX=cl.exe

set CACHE_ARGS=
where ccache >nul 2>nul
if not errorlevel 1 (
  set CACHE_ARGS=-DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  echo [build] ccache detected, enabled.
)

cmake --preset ninja %CACHE_ARGS%
if errorlevel 1 exit /b 1
cmake --build --preset %BUILD_PRESET%
if errorlevel 1 exit /b 1
ctest --test-dir out\build\ninja --output-on-failure
endlocal
