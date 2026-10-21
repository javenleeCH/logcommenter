cmake_minimum_required(VERSION 3.14)
project(logcommenter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/utf-8)
endif()

# 架构分层：
#  - 纯逻辑核心（logengine/）零 Clang 依赖，本机/快速可编译测试。
#  - AST 引擎 + CLI（clang/engine_clang + main）依赖 Clang/LibTooling，
#    由 LOGCOMMENTER_BUILD_AST 控制；统一用 CPM 现场编译 llvm-project，
#    CMake 保持简单，不依赖系统 LLVM。
option(LOGCOMMENTER_BUILD_AST "Build AST engine + CLI (needs Clang/LibTooling)" ON)

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

# ---- 3. AST engine + CLI ----
if(LOGCOMMENTER_BUILD_AST)

  # ---- AST 引擎统一走 CPM 现场编译 llvm-project（保持 CMake 简单）----
  include(cmake/CPM.cmake)
    CPMAddPackage(
      NAME LLVM_PROJECT
      URL https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-23.1.3.zip
      URL_HASH SHA256=2198f03ae4eb6bc6b71cd2a79c60ea9f9ce6bc6bc3a8dcf9e29861f425236746
      DOWNLOAD_ONLY YES
    )
    set(LLVM_ENABLE_PROJECTS "clang" CACHE STRING "启用 Clang")
    set(LLVM_TARGETS_TO_BUILD "X86" CACHE STRING "目标架构（可选）")
    set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "不构建测试")
    set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "不构建示例")
    set(LLVM_BUILD_TOOLS OFF CACHE BOOL "只构建库")
    set(LLVM_BUILD_UTILS OFF CACHE BOOL "")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "静态链接")
    set(LLVM_ENABLE_RTTI ON CACHE BOOL "Clang AST 需要 RTTI")
    set(LLVM_ENABLE_ASSERTIONS ON CACHE BOOL "启用调试断言")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型")
    # EXCLUDE_FROM_ALL：LLVM/Clang 所有 target 不进默认 all 目标，避免 `cmake --build`
    # 触发全量编译；只有被 logcommenter_engine 依赖的库才会被构建。
    add_subdirectory(${LLVM_PROJECT_SOURCE_DIR}/llvm ${LLVM_PROJECT_BINARY_DIR} EXCLUDE_FROM_ALL)

    add_library(logcommenter_engine src/clang/engine_clang.cpp)
    # 运行时给 ClangTool 指定 resource dir（静态链接无法从 argv[0] 推导）
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
    # EXCLUDE_FROM_ALL 使 clang 内置头目标默认不被构建，但 LibTooling 运行时依赖
    # 它们（resource-dir include/ 下的 stddef.h 等）。注意：LLVM 23 中内置头由
    # clang-resource-headers 生成到 <LLVM_BINARY_DIR>/lib/clang/<major>/include；
    # clang-headers 只是 install 组件别名，不产出文件。
    # 显式依赖，确保 --target logcommenter 时先生成内置头。
    add_dependencies(logcommenter_engine clang-resource-headers)

  add_executable(logcommenter src/main.cpp)
  target_link_libraries(logcommenter PRIVATE logcommenter_engine logcommenter_core)

  # 样例源码目标：让 sample.cpp 进入 compile_commands.json
  add_executable(logcommenter_sample src/sample.cpp)
  target_include_directories(logcommenter_sample PRIVATE ${LLVM_INCLUDE_DIRS})
endif()
