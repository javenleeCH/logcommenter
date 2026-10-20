#include "text_edit.h"

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

std::string editsToJson(const std::string &file,
                        const std::vector<TextEdit> &edits) {
  std::string out = "{\n  \"file\": \"" + jsonEscape(file) + "\",\n  \"edits\": [";
  for (size_t i = 0; i < edits.size(); ++i) {
    const TextEdit &e = edits[i];
    if (i) out += ",";
    out += "\n    { \"range\": { \"start\": { \"line\": " +
           std::to_string(e.start.line) + ", \"col\": " +
           std::to_string(e.start.col) +
           " }, \"end\": { \"line\": " + std::to_string(e.end.line) +
           ", \"col\": " + std::to_string(e.end.col) +
           " } }, \"text\": \"" + jsonEscape(e.text) + "\" }";
  }
  out += "\n  ]\n}\n";
  return out;
}

}  // namespace logc
