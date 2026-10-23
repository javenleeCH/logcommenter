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
#  - AST 引擎 + CLI 依赖预编译的 LLVM/Clang 静态库（release asset，见
#    .github/workflows/toolchain.yml；tools/fetch_llvm.sh 下载），通过 find_package 消费。
# 编译器与构建类型由 CMakePresets.json 控制（windows-msvc / unix-default），不在此处硬编码。
# 定位预编译工具链：设置 LLVM_DIR / Clang_DIR（CI 由 fetch 脚本输出），或本机已安装 LLVM。
option(LOGCOMMENTER_BUILD_AST "Build AST engine + CLI (needs LLVM/Clang)" ON)

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

# ---- 3. AST engine + CLI (prebuilt LLVM/Clang via find_package) ----
if(LOGCOMMENTER_BUILD_AST)
  # ClangConfig 会自动拉入 LLVM；显式写出以便报错信息清晰。
  find_package(LLVM REQUIRED CONFIG)
  find_package(Clang REQUIRED CONFIG)

  # 可选：预编译工具链根目录，用于 resource-dir 兑底（CI 下资源头复制到 exe 旁，一般无需）。
  set(LOGC_LLVM_BIN_DIR "." CACHE PATH "Optional LLVM dir for resource-dir fallback")

  add_library(logcommenter_engine src/clang/engine_clang.cpp)
  target_include_directories(logcommenter_engine PRIVATE
    ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS} src)
  target_compile_definitions(logcommenter_engine PRIVATE
    ${LLVM_DEFINITIONS}
    LOGC_LLVM_BIN_DIR="${LOGC_LLVM_BIN_DIR}")
  # 静态库依赖通过 imported targets 传递（ClangConfig 已配置好头目录/依赖/系统库）。
  target_link_libraries(logcommenter_engine PUBLIC logcommenter_core
    clangTooling clangASTMatchers clangRewrite clangFrontend
    clangSerialization clangAST clangBasic clangLex)

  add_executable(logcommenter src/main.cpp)
  target_link_libraries(logcommenter PRIVATE logcommenter_engine logcommenter_core)

  # 样例源码目标：让 sample.cpp 进入 compile_commands.json
  add_executable(logcommenter_sample src/sample.cpp)
  target_include_directories(logcommenter_sample PRIVATE ${LLVM_INCLUDE_DIRS})
endif()
