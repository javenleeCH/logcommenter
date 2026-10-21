// logcommenter CLI：参数解析 → 索引取编译参数 → Clang 引擎 → 输出 TextEdit JSON。
#include <cstdio>
#include <string>
#include <vector>

#include "clang/engine_clang.h"
#include "logengine/args.h"
#include "logengine/text_edit.h"

using namespace logc;

static void usage(const char *prog) {
  std::printf(
      "logcommenter - C++ 日志注入 / 注释工具\n"
      "\n"
      "用法:\n"
      "  %s --inject --file <path> --var <name> [--position line:col]\n"
      "      --compile-commands <compile_commands.json>\n"
      "  %s --comment --file <path> --compile-commands <compile_commands.json>\n"
      "\n"
      "选项:\n"
      "  --inject / --comment     动作：注入观测日志 / 注释标记日志\n"
      "  --file PATH              待处理源文件\n"
      "  --var NAME               注入：要观测的变量名\n"
      "  --position LINE:COL      注入：光标位置（1-based，可选）\n"
      "  --compile-commands PATH  compile_commands.json 路径\n"
      "  --help                   帮助\n",
      prog, prog);
}

int main(int argc, const char **argv) {
  CliArgs a = parseArgs(argc, argv);
  if (a.help) {
    usage(argv[0]);
    return 0;
  }
  if (!a.error.empty()) {
    std::fprintf(stderr, "error: %s\n", a.error.c_str());
    usage(argv[0]);
    return 2;
  }
  if (a.action == Action::kNone) {
    std::fprintf(stderr, "error: need --inject or --comment\n");
    usage(argv[0]);
    return 2;
  }
  if (a.file.empty()) {
    std::fprintf(stderr, "error: need --file\n");
    usage(argv[0]);
    return 2;
  }
  if (a.compileJson.empty()) {
    std::fprintf(stderr, "error: need --compile-commands <path>\n");
    usage(argv[0]);
    return 2;
  }

  std::vector<TextEdit> edits;
  std::string err;
  if (!runClangEngine(a, a.compileJson, argv[0], &edits, &err)) {
    std::fprintf(stderr, "error: %s\n", err.c_str());
    return 1;
  }
  std::fputs(editsToJson(a.file, edits).c_str(), stdout);
  return 0;
}
