cmake_minimum_required(VERSION 3.21)

project(logcommenter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8)
endif()

# 分层：
#  - 纯逻辑核心 logengine/ 零 Clang 依赖，可快速编译测试；
#  - AST 引擎 + CLI 依赖 Clang/LibTooling，用 CPM 现场编译 llvm-project。
# 编译器与构建类型由 CMakePresets.json 控制（windows-msvc / unix-default），不在此处硬编码。
option(LOGCOMMENTER_BUILD_AST "Build AST engine + CLI (needs Clang/LibTooling)" ON)
# 唯一的 SSL 开关：代理/自签证书环境下禁用所有依赖下载（CPM.cmake 引导 + llvm-project）的 TLS 校验。
option(LOGC_DISABLE_SSL "Disable TLS verification for all dependency downloads" OFF)

# ---- 1. core library (zero Clang deps) ----
add_library(logcommenter_core
  src/logengine/format_table.cpp
  src/logengine/text_edit.cpp
  src/logengine/compile_index.cpp
  src/logengine/args.cpp
)
target_include_directories(logcommenter_core PUBLIC src)

# ---- 2. unit tests (core only) ----
enable_testing()
add_executable(logcommenter_tests
  tests/test_main.cpp
  tests/test_format_table.cpp
  tests/test_text_edit.cpp
  tests/test_compile_index.cpp
  tests/test_args.cpp
)
target_link_libraries(logcommenter_tests PRIVATE logcommenter_core)
add_test(NAME core_tests COMMAND logcommenter_tests)

# ---- 3. AST engine + CLI (Clang/LibTooling via CPM) ----
if(LOGCOMMENTER_BUILD_AST)
  # LOGC_DISABLE_SSL 直接作用到全局 CMAKE_TLS_VERIFY（覆盖 CPM.cmake 引导下载等一切
  # file(DOWNLOAD)）；再置 CPM_DOWNLOAD_TLS_VERIFY=OFF，以覆盖 CPM 内部拉取 llvm-project。
  if(LOGC_DISABLE_SSL)
    set(CMAKE_TLS_VERIFY FALSE CACHE BOOL "" FORCE)
    set(CPM_DOWNLOAD_TLS_VERIFY OFF)
  endif()

  include(cmake/CPM.cmake)

  CPMAddPackage(
    NAME LLVM_PROJECT
    URL https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-23.1.3.zip
    URL_HASH SHA256=2198f03ae4eb6bc6b71cd2a79c60ea9f9ce6bc6bc3a8dcf9e29861f425236746
    DOWNLOAD_ONLY YES
  )

  set(LLVM_ENABLE_PROJECTS "clang" CACHE STRING "Enable Clang")
  set(LLVM_TARGETS_TO_BUILD "X86" CACHE STRING "Target architectures")
  set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "Skip LLVM tests")
  set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "Skip LLVM examples")
  set(LLVM_BUILD_TOOLS OFF CACHE BOOL "Libraries only")
  set(LLVM_BUILD_UTILS OFF CACHE BOOL "Skip utils")
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static linking")
  set(LLVM_ENABLE_RTTI ON CACHE BOOL "RTTI required by Clang AST")
  set(LLVM_ENABLE_ASSERTIONS ON CACHE BOOL "Enable debug assertions")
  # EXCLUDE_FROM_ALL：LLVM/Clang targets 不进默认 all，只构建被依赖的库，
  # 避免 `cmake --build` 触发全量编译。
  add_subdirectory(${LLVM_PROJECT_SOURCE_DIR}/llvm ${LLVM_PROJECT_BINARY_DIR} EXCLUDE_FROM_ALL)

  add_library(logcommenter_engine src/clang/engine_clang.cpp)
  target_compile_definitions(logcommenter_engine PRIVATE
    LOGC_LLVM_BIN_DIR="${LLVM_PROJECT_BINARY_DIR}")
  target_include_directories(logcommenter_engine PRIVATE
    ${LLVM_PROJECT_SOURCE_DIR}/llvm/include
    ${LLVM_PROJECT_SOURCE_DIR}/clang/include
    ${LLVM_PROJECT_BINARY_DIR}/include
    ${LLVM_PROJECT_BINARY_DIR}/tools/clang/include
    src)
  target_link_directories(logcommenter_engine PRIVATE ${LLVM_LIBRARY_DIRS})
  target_link_libraries(logcommenter_engine PUBLIC logcommenter_core
    clangTooling clangASTMatchers clangRewrite clangFrontend
    clangSerialization clangAST clangBasic clangLex)
  # 显式依赖，确保 --target logcommenter 时先生成 clang 内置头（resource-dir include/）。
  add_dependencies(logcommenter_engine clang-resource-headers)

  add_executable(logcommenter src/main.cpp)
  target_link_libraries(logcommenter PRIVATE logcommenter_engine logcommenter_core)

  # 样例源码目标：让 sample.cpp 进入 compile_commands.json
  add_executable(logcommenter_sample src/sample.cpp)
  target_include_directories(logcommenter_sample PRIVATE ${LLVM_INCLUDE_DIRS})
endif()
