@echo off
REM One-shot configure + build + test with Ninja (Windows MSVC).
REM Run inside MSVC dev environment (vcvarsall / msvc-dev-cmd).
setlocal
cd /d "%~dp0"

set "PRESET=windows-msvc"
set "CACHE_ARGS="
where ccache >nul 2>nul
if not errorlevel 1 (
  set "CACHE_ARGS=-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
  echo [build] ccache detected, enabled.
)

cmake --preset %PRESET% %CACHE_ARGS%
if errorlevel 1 exit /b 1
cmake --build build\%PRESET%
if errorlevel 1 exit /b 1
ctest --test-dir build\%PRESET% --output-on-failure
endlocal
