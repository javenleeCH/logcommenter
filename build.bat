@echo off
REM One-shot configure + build + test with Ninja.
REM Usage: build.bat [debug|release] [compiler]
REM   compiler: auto(默认，用环境 CC/CXX) | msvc | clang-cl
REM 网络代理请通过环境变量 http_proxy/https_proxy 自行设置，脚本不预设。
setlocal
cd /d "%~dp0"

set "MODE=%~1"
if "%MODE%"=="" set "MODE=debug"
set "COMPILER=%~2"
if "%COMPILER%"=="" set "COMPILER=auto"

set "PRESET=ninja"
if /I "%COMPILER%"=="msvc"     set "PRESET=ninja-msvc"
if /I "%COMPILER%"=="clang-cl" set "PRESET=ninja-clang-cl"

if /I "%MODE%"=="release" (set "BUILD_PRESET=%PRESET%-release") else (set "BUILD_PRESET=%PRESET%-debug")

set "CACHE_ARGS="
where ccache >nul 2>nul
if not errorlevel 1 (
  set "CACHE_ARGS=-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
  echo [build] ccache detected, enabled.
)

cmake --preset %PRESET% %CACHE_ARGS%
if errorlevel 1 exit /b 1
cmake --build --preset %BUILD_PRESET%
if errorlevel 1 exit /b 1
ctest --test-dir out\build\%PRESET% --output-on-failure
endlocal
