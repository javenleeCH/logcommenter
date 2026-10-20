cmake_minimum_required(VERSION 3.14)
project(logcommenter)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/utf-8)
endif()


include(cmake/CPM.cmake)


# 使用 CPM 获取 llvm-project 源码
CPMAddPackage(
  NAME LLVM_PROJECT
  GITHUB_REPOSITORY llvm/llvm-project
  GIT_TAG llvmorg-21.1.4
  DOWNLOAD_ONLY YES
)

# 设置 Clang 构建选项
# -----------------------------
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

# -----------------------------
# 添加子目录：构建 LLVM + Clang
# -----------------------------
add_subdirectory(${LLVM_PROJECT_SOURCE_DIR}/llvm ${LLVM_PROJECT_BINARY_DIR})

# 添加LLVM包含目录
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})



# 主工具可执行文件
add_executable(logcommenter src/main.cpp)


target_include_directories(logcommenter PRIVATE
  # LLVM 和 Clang 的公共头文件
  ${LLVM_PROJECT_SOURCE_DIR}/llvm/include
  ${LLVM_PROJECT_SOURCE_DIR}/clang/include
  # 构建生成的头文件（重要！）
  ${LLVM_PROJECT_BINARY_DIR}/include
  ${LLVM_PROJECT_BINARY_DIR}/tools/clang/include
)

target_link_directories(logcommenter PRIVATE ${LLVM_LIBRARY_DIRS})


# 链接LLVM库
target_link_libraries(logcommenter clangTooling
  clangASTMatchers
  clangRewrite
  clangFrontend
  clangSerialization
  clangAST
  clangBasic
  clangLex)


# 样例源码目标：让 sample.cpp 进入 compile_commands.json，供 logcommenter 处理
add_executable(logcommenter_sample src/sample.cpp)
target_include_directories(logcommenter_sample PRIVATE ${LLVM_INCLUDE_DIRS})
