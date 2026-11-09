#pragma once
// Clang 适配层：基于 LibTooling 在指定源文件上执行 inject/comment，产出 TextEdit。
// 依赖 Clang/LibTooling，由 CMake 的 LOGCOMMENTER_BUILD_AST 控制是否构建。
#include <string>
#include <vector>

#include "../logengine/args.h"
#include "../logengine/text_edit.h"

namespace logc {

// 构建 --file 的 PCH 缓存（<compileJson>.logc-pch/）：由 runClangEngine 在缓存
// 缺失时 detach 自调用（--logc-internal-pch 隐藏旗标），也可显式调用预热。
// 成功返回 true；err 承载失败原因。
bool buildPchCache(const CliArgs &args, const std::string &compileJson,
                   const std::string &exePath, std::string *err);

// 对 file 执行 args 指定的动作；编译参数从 compileJson（compile_commands.json）
// 中按偏移索引精准取一条，不加载全量。exePath 为当前可执行文件路径（argv[0]），
// 用于运行时定位随包分发的 clang 内置头目录（clang-headers），不依赖构建机路径。
// 成功返回 true 并把改动写入 edits。
// 查不到编译条目时按规划 §6.1 降级为模板日志（不中断操作）：edits 仍可能有
// 内容，warn 承载给用户的提示（非空时调用方应转发到 stderr/插件提示）。
bool runClangEngine(const CliArgs &args, const std::string &compileJson,
                    const std::string &exePath,
                    std::vector<TextEdit> *edits, std::string *err,
                    std::string *warn = nullptr);

}  // namespace logc
