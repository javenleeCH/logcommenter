#include "format_table.h"

#include <algorithm>
#include <cctype>

namespace logc {

std::string simplifyType(const std::string &qualType) {
  std::string s = qualType;
  // 逐 token 去除 const/volatile/引用/struct/class 等修饰，保留核心类型
  const char *tokens[] = {"const", "volatile", "struct", "class", "&", "&&"};
  for (const char *tok : tokens) {
    std::string needle(tok);
    size_t pos = 0;
    while ((pos = s.find(needle, pos)) != std::string::npos) {
      // 仅在独立 token（两侧非字母数字下划线）时删除，避免误删子串
      auto is_sep = [](char c) { return !std::isalnum((unsigned char)c) && c != '_'; };
      bool left_ok = pos == 0 || is_sep(s[pos - 1]);
      bool right_ok = pos + needle.size() == s.size() ||
                      is_sep(s[pos + needle.size()]);
      if (left_ok && right_ok) {
        s.erase(pos, needle.size());
      } else {
        ++pos;
      }
    }
  }
  // 压缩多余空白
  std::string out;
  bool in_ws = false;
  for (char c : s) {
    if (std::isspace((unsigned char)c)) {
      in_ws = true;
    } else {
      if (in_ws && !out.empty()) out += ' ';
      out += c;
      in_ws = false;
    }
  }
  // 去掉残留的 * （指针保留其后的类型主体），本函数只用于类型识别
  out.erase(std::remove_if(out.begin(), out.end(),
                           [](char c) { return c == '*'; }),
            out.end());
  // 再次压缩
  std::string trimmed;
  bool ws = false;
  for (char c : out) {
    if (std::isspace((unsigned char)c)) {
      ws = true;
    } else {
      if (ws && !trimmed.empty()) trimmed += ' ';
      trimmed += c;
      ws = false;
    }
  }
  return trimmed;
}

bool isPrimitiveStreamable(const std::string &simplified) {
  // 基础数值
  static const char *prims[] = {
      "int",   "long",  "short",  "float",  "double", "char",
      "bool",  "size_t", "unsigned", "signed", "unsigned int",
      "unsigned long", "long long", "unsigned long long", "wchar_t", "auto"};
  for (const char *p : prims) {
    if (simplified == p) return true;
  }
  // 字符 / 字符串指针
  if (simplified == "char *" || simplified == "const char *" ||
      simplified == "char *" || simplified == "const char*" ||
      simplified.find("char") != std::string::npos &&
          simplified.find("*") != std::string::npos) {
    return true;
  }
  // std::string 系
  if (simplified.find("std::string") != std::string::npos ||
      simplified.find("std::basic_string") != std::string::npos) {
    return true;
  }
  return false;
}

ObsFormat formatForType(const std::string &qualType, const std::string &var) {
  std::string simp = simplifyType(qualType);
  ObsFormat f;
  if (isPrimitiveStreamable(simp)) {
    f.streamable = true;
    // bool 可读化
    if (simp == "bool") {
      f.expr = "(" + var + " ? \"true\" : \"false\")";
    } else {
      f.expr = var;
    }
    return f;
  }
  // 指针（非 char*）：打印地址
  if (qualType.find('*') != std::string::npos &&
      qualType.find("char") == std::string::npos) {
    f.streamable = true;
    f.expr = var;
    return f;
  }
  // 其余（对象等）：不可直接流式打印，交由上层处理
  f.streamable = false;
  return f;
}

}  // namespace logc
