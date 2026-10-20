#pragma once
// CLI 参数解析：把 argv 解析为请求结构。纯逻辑，可独立单测。
#include <string>

namespace logc {

enum class Action { kInject, kComment, kNone };

struct CliArgs {
  Action action = Action::kNone;
  std::string file;
  std::string var;       // inject 用：选中的变量名
  int line = -1;         // inject/comment 用：选中行（1-based）
  int col = -1;          // inject 用：选中列（1-based）
  std::string compileJson;  // compile_commands.json 路径
  bool help = false;
  std::string error;     // 非空表示解析失败
};

// 解析命令行。失败返回 error 非空。
CliArgs parseArgs(int argc, const char **argv);

// 解析 "line:col" 形式的选区；返回 true 且写 line/col
bool parsePosition(const std::string &pos, int *line, int *col);

}  // namespace logc
