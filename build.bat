@echo off
REM One-shot fetch prebuilt LLVM + configure + build + test with Ninja (Windows MSVC).
REM Usage: build.bat [release|debug]
REM   release (default): Release LLVM (.toolchain) + windows-msvc preset (Release), build\windows-msvc\
REM   debug: Debug LLVM (.toolchain-debug, with -g) + windows-msvc-debug preset (Debug),
REM          build\windows-msvc-debug\, for stepping into LLVM.
REM Run inside MSVC dev environment (vcvarsall / msvc-dev-cmd). Requires bash (Git for Windows).
setlocal
cd /d "%~dp0"

set "MODE=%~1"
if "%MODE%"=="" set "MODE=release"
if /i "%MODE%"=="release" (
  set "PRESET=windows-msvc"
  set "TC=%CD%\.toolchain"
  set "TCARG="
) else if /i "%MODE%"=="debug" (
  set "PRESET=windows-msvc-debug"
  set "TC=%CD%\.toolchain-debug"
  set "TCARG=debug"
) else (
  echo usage: build.bat [release^|debug]
  exit /b 1
)

if not exist "%TC%\lib\cmake\llvm\LLVMConfig.cmake" (
  bash tools/fetch_llvm.sh "" "%TC%" "%TCARG%"
  if errorlevel 1 exit /b 1
)
cmake --preset %PRESET% -DLLVM_DIR="%TC%\lib\cmake\llvm" -DClang_DIR="%TC%\lib\cmake\clang"
if errorlevel 1 exit /b 1
cmake --build build\%PRESET%
if errorlevel 1 exit /b 1
ctest --test-dir build\%PRESET% --output-on-failure
endlocal
