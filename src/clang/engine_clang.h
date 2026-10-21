#pragma once
// Clang 适配层：基于 LibTooling 在指定源文件上执行 inject/comment，产出 TextEdit。
// 依赖 Clang/LibTooling，由 CMake 的 LOGCOMMENTER_BUILD_AST 控制是否构建。
#include <string>
#include <vector>

#include "../logengine/args.h"
#include "../logengine/text_edit.h"

namespace logc {

// 对 file 执行 args 指定的动作；编译参数从 compileJson（compile_commands.json）
// 中按偏移索引精准取一条，不加载全量。exePath 为当前可执行文件路径（argv[0]），
// 用于运行时定位随包分发的 clang 内置头目录（clang-headers），不依赖构建机路径。
// 成功返回 true 并把改动写入 edits。
bool runClangEngine(const CliArgs &args, const std::string &compileJson,
                    const std::string &exePath,
                    std::vector<TextEdit> *edits, std::string *err);

}  // namespace logc
