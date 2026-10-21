@echo off
REM One-shot configure + build + test with Ninja.
REM Usage: build.bat [debug|release]   (default: debug)
setlocal
cd /d "%~dp0"

set "MODE=%~1"
if "%MODE%"=="" set "MODE=debug"
if /I "%MODE%"=="release" (set "BUILD_PRESET=ninja-release") else (set "BUILD_PRESET=ninja-debug")

REM 代理：CPM 拉取 llvm-project 需要（默认 127.0.0.1:4780，可用环境变量覆盖）
if not defined http_proxy set "http_proxy=http://127.0.0.1:4780"
if not defined https_proxy set "https_proxy=http://127.0.0.1:4780"

if not defined CC set "CC=cl.exe"
if not defined CXX set "CXX=cl.exe"

set "CACHE_ARGS="
where ccache >nul 2>nul
if not errorlevel 1 (
  set "CACHE_ARGS=-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
  echo [build] ccache detected, enabled.
)

cmake --preset ninja %CACHE_ARGS%
if errorlevel 1 exit /b 1
cmake --build --preset %BUILD_PRESET%
if errorlevel 1 exit /b 1
ctest --test-dir out\build\ninja --output-on-failure
endlocal