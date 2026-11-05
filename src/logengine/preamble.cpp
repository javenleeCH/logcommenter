#include "preamble.h"

#include <string>

namespace logc {

namespace {
std::string ltrim(std::string s) {
  size_t b = s.find_first_not_of(" \t\r\v\f");
  return b == std::string::npos ? std::string() : s.substr(b);
}
}  // namespace

std::string extractPreamble(const std::string &text) {
  const size_t n = text.size();
  size_t i = 0;
  std::string out;
  // BOM 原样保留（写出的 preamble 文件仍带 BOM，重解析语义一致）。
  if (n >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
      (unsigned char)text[2] == 0xBF) {
    out += text.substr(0, 3);
    i = 3;
  }
  bool inBlock = false;  // 处于跨行块注释内
  bool sawInclude = false;
  while (i < n) {
    size_t e = text.find('\n', i);
    size_t end = (e == std::string::npos) ? n : e;
    std::string line = text.substr(i, end - i);
    if (!line.empty() && line.back() == '\r') line.pop_back();  // CRLF → LF
    if (inBlock) {
      // 注释体行：原样保留；行内闭合时只保留到 "*/"（其后是代码）。
      size_t close = line.find("*/");
      if (close != std::string::npos) {
        out += line.substr(0, close + 2);
        out += '\n';
        inBlock = false;
      } else {
        out += line;
        out += '\n';
      }
    } else {
      std::string t = ltrim(line);
      if (t.empty() || t[0] == '#' || t.rfind("//", 0) == 0) {
        // 空行 / 预处理指令 / 行注释：整行保留。
        out += line;
        out += '\n';
        if (t.rfind("#include", 0) == 0) sawInclude = true;
        // 指令行也可能开启跨行块注释（如 `#include <a> /* ...`）。
        // 字符串/字符字面量内的标记不算（字符串感知扫描）。
        bool inS = false, inChr = false;
        for (size_t k = 0; k + 1 < line.size(); ++k) {
          char c = line[k];
          if (inS || inChr) {
            if (c == '\\') { ++k; continue; }
            if ((inS && c == '"') || (inChr && c == '\'')) inS = inChr = false;
          } else if (c == '"') inS = true;
          else if (c == '\'') inChr = true;
          else if (c == '/' && line[k + 1] == '*') { inBlock = true; break; }
          else if (c == '/' && line[k + 1] == '/') break;  // 行注释
        }
      } else {
        // 代码行：置空占位保行号，但保留行内真实注释部分（否则跨行注释
        // 结构断裂、后续注释体行成为悬空文本）。字符串字面量里的 "/*"
        // 不算注释。行内 `//` 注释内容直接丢弃（注释无副作用）。
        std::string emit;
        bool block = false;
        size_t k = 0;
        int inS = 0;  // 0 无 / 1 字符串 / 2 字符
        while (k < line.size()) {
          char c = line[k];
          if (inS) {
            if (c == '\\') { k += 2; continue; }
            if ((inS == 1 && c == '"') || (inS == 2 && c == '\'')) inS = 0;
            ++k;
            continue;
          }
          if (c == '"') { inS = 1; ++k; continue; }
          if (c == '\'') { inS = 2; ++k; continue; }
          if (c == '/' && k + 1 < line.size() && line[k + 1] == '*') {
            size_t close = line.find("*/", k + 2);
            if (close == std::string::npos) {
              emit += line.substr(k);  // 跨行注释开启
              block = true;
              break;
            }
            emit += line.substr(k, close - k + 2);  // 行内闭合的注释
            k = close + 2;
            continue;
          }
          if (c == '/' && k + 1 < line.size() && line[k + 1] == '/') break;
          ++k;
        }
        out += emit;
        out += '\n';
        inBlock = block;
      }
    }
    if (e == std::string::npos) break;
    i = e + 1;
  }
  if (!sawInclude) return "";  // 无 #include 的 preamble 对 PCH 无收益
  return out;
}

}  // namespace logc
