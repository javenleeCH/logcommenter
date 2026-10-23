#include "text_edit.h"

#include <map>
#include <vector>

namespace logc {

std::string jsonEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
}

namespace {
// 渲染单个 edit 对象（不含分组 file 字段）
std::string editJson(const TextEdit &e) {
  return "{ \"range\": { \"start\": { \"line\": " +
         std::to_string(e.start.line) + ", \"col\": " +
         std::to_string(e.start.col) +
         " }, \"end\": { \"line\": " + std::to_string(e.end.line) +
         ", \"col\": " + std::to_string(e.end.col) +
         " } }, \"text\": \"" + jsonEscape(e.text) + "\" }";
}

// 渲染一组文件 edits
std::string groupJson(const std::string &file,
                      const std::vector<TextEdit> &edits) {
  std::string out = "{ \"file\": \"" + jsonEscape(file) +
                    "\", \"edits\": [";
  for (size_t i = 0; i < edits.size(); ++i) {
    if (i) out += ",";
    out += "\n    " + editJson(edits[i]);
  }
  out += "\n  ] }";
  return out;
}
}  // namespace

std::string editsToJson(const std::string &file,
                        const std::vector<TextEdit> &edits) {
  // 单文件兼容路径：所有 edit.file 为空 → 输出原单对象格式
  //   { "file": ..., "edits": [...] }
  bool multi = false;
  for (auto &e : edits)
    if (!e.file.empty()) {
      multi = true;
      break;
    }
  if (!multi) {
    std::string out =
        "{\n  \"file\": \"" + jsonEscape(file) + "\",\n  \"edits\": [";
    for (size_t i = 0; i < edits.size(); ++i) {
      if (i) out += ",";
      out += "\n    " + editJson(edits[i]);
    }
    out += "\n  ]\n}\n";
    return out;
  }
  // 跨文件：按 file 分组（空 file 归入主 file），保持首次出现顺序
  std::vector<std::string> order;
  std::map<std::string, std::vector<TextEdit>> groups;
  for (auto &e : edits) {
    std::string f = e.file.empty() ? file : e.file;
    if (groups.find(f) == groups.end()) order.push_back(f);
    TextEdit c = e;
    c.file.clear();  // 组内不再重复输出 file
    groups[f].push_back(c);
  }
  std::string out = "[";
  for (size_t i = 0; i < order.size(); ++i) {
    if (i) out += ",";
    out += "\n  " + groupJson(order[i], groups[order[i]]);
  }
  out += "\n]\n";
  return out;
}

}  // namespace logc
