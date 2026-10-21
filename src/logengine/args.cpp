#include "args.h"

#include <cstdlib>
#include <cstring>

namespace logc {

namespace {
std::string normSlash(std::string s) {
  for (auto &c : s)
    if (c == '\\') c = '/';
  return s;
}
std::string baseName(const std::string &s) {
  auto p = s.find_last_of("/\\");
  return p == std::string::npos ? s : s.substr(p + 1);
}
bool isSourceFile(const std::string &s) {
  auto p = s.rfind('.');
  if (p == std::string::npos) return false;
  std::string ext = s.substr(p);
  return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" ||
         ext == ".C";
}
}  // namespace

bool parsePosition(const std::string &pos, int *line, int *col) {
  size_t c = pos.find(':');
  if (c == std::string::npos) return false;
  std::string ls = pos.substr(0, c);
  std::string cs = pos.substr(c + 1);
  if (ls.empty() || cs.empty()) return false;
  char *endL = nullptr, *endC = nullptr;
  long l = std::strtol(ls.c_str(), &endL, 10);
  long cc = std::strtol(cs.c_str(), &endC, 10);
  if (*endL != '\0' || *endC != '\0') return false;
  if (l <= 0 || cc <= 0) return false;
  *line = static_cast<int>(l);
  *col = static_cast<int>(cc);
  return true;
}

CliArgs parseArgs(int argc, const char **argv) {
  CliArgs a;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next = [&](const char *name, std::string *out) -> bool {
      if (i + 1 >= argc) return false;
      *out = argv[i + 1];
      ++i;
      return true;
    };
    if (arg == "--inject" || arg == "--action" && (argc > i + 1 &&
        std::string(argv[i + 1]) == "inject" && (++i, true))) {
      a.action = Action::kInject;
    } else if (arg == "--comment" || arg == "--action" && (argc > i + 1 &&
               std::string(argv[i + 1]) == "comment" && (++i, true))) {
      a.action = Action::kComment;
    } else if (arg == "--file") {
      if (!next("--file", &a.file)) { a.error = "--file requires a path"; return a; }
    } else if (arg == "--var") {
      if (!next("--var", &a.var)) { a.error = "--var requires a name"; return a; }
    } else if (arg == "--position") {
      std::string p;
      if (!next("--position", &p)) { a.error = "--position requires line:col"; return a; }
      if (!parsePosition(p, &a.line, &a.col)) {
        a.error = "invalid --position: " + p;
        return a;
      }
    } else if (arg == "--compile-commands") {
      if (!next("--compile-commands", &a.compileJson)) {
        a.error = "--compile-commands requires a path";
        return a;
      }
    } else if (arg == "--help" || arg == "-h") {
      a.help = true;
    } else {
      a.error = "unknown option: " + arg;
      return a;
    }
  }
  return a;
}

std::vector<std::string> stripSourceFileArg(const std::vector<std::string> &args,
                                            const std::string &sourceFile) {
  std::string fn = normSlash(sourceFile);
  std::string fnBase = baseName(fn);
  std::vector<std::string> out;
  out.reserve(args.size());
  for (auto &a : args) {
    // 归一后相等，或文件名相同且是源文件 → 视为 command 里重复的源文件 token，剥离。
    std::string na = normSlash(a);
    bool dupSource = !fn.empty() && isSourceFile(a) &&
                     (na == fn || baseName(na) == fnBase);
    if (!dupSource) out.push_back(a);
  }
  return out;
}

}  // namespace logc
