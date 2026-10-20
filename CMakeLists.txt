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
#    由 LOGCOMMENTER_BUILD_AST 控制；LOGCOMMENTER_SYSTEM_LLVM=ON 时用系统
#    LLVM（CI），否则用 CPM 现场编译 llvm-project（生产/本地全量）。
option(LOGCOMMENTER_BUILD_AST "Build AST engine + CLI (needs Clang/LibTooling)" ON)
option(LOGCOMMENTER_SYSTEM_LLVM "Use system LLVM/Clang instead of CPM build" OFF)

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

  if(LOGCOMMENTER_SYSTEM_LLVM)
    # 系统 clang 包通常不打包 LibTooling 头（clang/AST/*.h），这些头只能来自
    # LLVM 源码树。CI 可用 -DLLVM_SRC=<llvm-project 浅克隆目录> 补齐。
    set(LLVM_SRC "" CACHE PATH "LLVM/Clang source tree supplying clang headers")
    find_program(LLVM_CONFIG
      NAMES llvm-config-21 llvm-config-20 llvm-config-19 llvm-config-18
            llvm-config-17 llvm-config-16 llvm-config-15 llvm-config-14
            llvm-config)
    if(NOT LLVM_CONFIG)
      # 兜底：在常见安装路径（/usr/lib/llvm-*/bin、Homebrew 等）下查找
      file(GLOB LLVM_CONFIG_CANDIDATES
        /usr/lib/llvm-*/bin/llvm-config* /usr/local/opt/llvm/bin/llvm-config*)
      if(LLVM_CONFIG_CANDIDATES)
        list(GET LLVM_CONFIG_CANDIDATES 0 LLVM_CONFIG)
      endif()
    endif()
    if(NOT LLVM_CONFIG)
      message(FATAL_ERROR "llvm-config not found; set LOGCOMMENTER_BUILD_AST=OFF or install LLVM/Clang")
    endif()
    execute_process(COMMAND ${LLVM_CONFIG} --includedir
      OUTPUT_VARIABLE LLVM_INC OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG} --libdir
      OUTPUT_VARIABLE LLVM_LIB OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG} --cxxflags
      OUTPUT_VARIABLE LLVM_CXXFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG} --version
      OUTPUT_VARIABLE LLVM_CONFIG_VER OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "llvm-config: ${LLVM_CONFIG}")
    message(STATUS "llvm version: ${LLVM_CONFIG_VER}")
    message(STATUS "LLVM include dir: ${LLVM_INC}")
    # clang 的 C++ 头文件（clang/AST/*.h）未必位于 ${LLVM_INC} 下（Ubuntu 各发行版
    # 摆放位置不一），主动在常见路径搜索并收集其父目录。
    set(CLANG_INC_DIRS "")
    file(GLOB _clang_roots /usr/lib/llvm-*/include /usr/include /usr/lib/llvm-*/lib/clang/*/include)
    foreach(_r IN LISTS _clang_roots)
      if(EXISTS "${_r}/clang/AST/ASTConsumer.h")
        list(APPEND CLANG_INC_DIRS "${_r}")
      endif()
    endforeach()
    if(NOT CLANG_INC_DIRS AND EXISTS "${LLVM_INC}/clang/AST/ASTConsumer.h")
      list(APPEND CLANG_INC_DIRS "${LLVM_INC}")
    endif()
    if(NOT CLANG_INC_DIRS AND LLVM_SRC)
      if(EXISTS "${LLVM_SRC}/clang/include/clang/AST/ASTConsumer.h")
        list(APPEND CLANG_INC_DIRS "${LLVM_SRC}/clang/include")
      endif()
      if(EXISTS "${LLVM_SRC}/llvm/include/llvm/IR/Module.h")
        list(APPEND CLANG_INC_DIRS "${LLVM_SRC}/llvm/include")
      endif()
    endif()
    if(NOT CLANG_INC_DIRS)
      message(FATAL_ERROR "clang C++ headers (clang/AST/ASTConsumer.h) not found; install a clang dev package (e.g. libclang-common-<ver>-dev / clang-<ver>)")
    endif()
    message(STATUS "clang header dirs: ${CLANG_INC_DIRS}")

    add_library(logcommenter_engine src/clang/engine_clang.cpp)
    target_include_directories(logcommenter_engine PRIVATE ${LLVM_INC} ${CLANG_INC_DIRS} src)
    target_compile_options(logcommenter_engine PRIVATE ${LLVM_CXXFLAGS})
    target_link_directories(logcommenter_engine PRIVATE ${LLVM_LIB})
    target_link_libraries(logcommenter_engine PUBLIC logcommenter_core clang-cpp)
  else()
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
    # EXCLUDE_FROM_ALL 使 clang 内置头目标（clang-headers）默认不被构建，
    # 但 LibTooling 运行时依赖它们（resource dir 里的 stddef.h 等）。
    # 显式依赖，确保 --target logcommenter 时先生成内置头。
    add_dependencies(logcommenter_engine clang-headers)
  endif()

  add_executable(logcommenter src/main.cpp)
  target_link_libraries(logcommenter PRIVATE logcommenter_engine logcommenter_core)

  # 样例源码目标：让 sample.cpp 进入 compile_commands.json
  add_executable(logcommenter_sample src/sample.cpp)
  if(LOGCOMMENTER_SYSTEM_LLVM)
    target_include_directories(logcommenter_sample PRIVATE ${LLVM_INC})
  else()
    target_include_directories(logcommenter_sample PRIVATE ${LLVM_INCLUDE_DIRS})
  endif()
endif()
