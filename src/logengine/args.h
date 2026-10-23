#pragma once
// CLI 参数解析：把 argv 解析为请求结构。纯逻辑，可独立单测。
#include <string>
#include <vector>

namespace logc {

enum class Action { kInject, kComment, kNone };

struct CliArgs {
  Action action = Action::kNone;
  std::string file;
  std::string var;       // inject 用：选中的变量名
  int line = -1;         // inject/comment 用：选中行（1-based）
  int col = -1;          // inject 用：选中列（1-based）
  std::string compileJson;  // compile_commands.json 路径
  bool friend_ = false;     // inject 用：对象不可流式时注入 inline friend operator<<
                            // （改类所在头文件，需用户显式确认）
  bool help = false;
  std::string error;     // 非空表示解析失败
};

// 解析命令行。失败返回 error 非空。
CliArgs parseArgs(int argc, const char **argv);

// 解析 "line:col" 形式的选区；返回 true 且写 line/col
bool parsePosition(const std::string &pos, int *line, int *col);

// 从编译命令行参数中剥离与目标源文件重复的源文件 token。
// MSVC(CMake) compile_commands 的 command 字段常自带源文件路径 + /Fo 输出参数，
// 而 ClangTool 又会把 --file 的源文件注入一次，导致"多个输入 + /Fo 单输出"冲突
// （cannot specify '/Fo' when compiling multiple source files）。返回过滤后的参数。
std::vector<std::string> stripSourceFileArg(const std::vector<std::string> &args,
                                            const std::string &sourceFile);

}  // namespace logc
