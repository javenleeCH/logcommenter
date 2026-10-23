@echo off
REM One-shot fetch prebuilt LLVM + configure + build + test with Ninja (Windows MSVC).
REM Run inside MSVC dev environment (vcvarsall / msvc-dev-cmd). Requires bash (Git for Windows).
setlocal
cd /d "%~dp0"

set "PRESET=windows-msvc"
if not exist .toolchain\lib\cmake\llvm\LLVMConfig.cmake (
  bash tools/fetch_llvm.sh
  if errorlevel 1 exit /b 1
)
set "TC=%CD%\.toolchain"
cmake --preset %PRESET% -DLLVM_DIR="%TC%\lib\cmake\llvm" -DClang_DIR="%TC%\lib\cmake\clang"
if errorlevel 1 exit /b 1
cmake --build build\%PRESET%
if errorlevel 1 exit /b 1
ctest --test-dir build\%PRESET% --output-on-failure
endlocal
