#include "file_apply.h"

#include <algorithm>
#include <fstream>
#include <map>

namespace logc {

namespace {

bool readFileBytes(const std::string &path, std::string *out,
                   std::string *err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    *err = "cannot open for read: " + path;
    return false;
  }
  out->assign((std::istreambuf_iterator<char>(f)),
              std::istreambuf_iterator<char>());
  if (f.bad()) {
    *err = "read failed: " + path;
    return false;
  }
  return true;
}

bool writeFileBytes(const std::string &path, const std::string &data,
                    std::string *err) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    *err = "cannot open for write: " + path;
    return false;
  }
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!f) {
    *err = "write failed: " + path;
    return false;
  }
  return true;
}

// 切行（保留行尾符在行尾，便于按字节列定位 + 原样回写行尾风格）。
// 文件以 \n 结尾时末尾会有一个空串行，拼接时自然还原。
void splitLines(const std::string &data, std::vector<std::string> *lines,
                bool *crlf) {
  *crlf = data.find("\r\n") != std::string::npos;
  lines->clear();
  std::string cur;
  for (size_t i = 0; i < data.size(); ++i) {
    char c = data[i];
    if (c == '\n') {
      if (!cur.empty() && cur.back() == '\r') cur.pop_back();
      lines->push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  lines->push_back(cur);  // 最后一段（可能带 \r，无 \n 的老 Mac 风格行尾）
}

std::string joinLines(const std::vector<std::string> &lines, bool crlf) {
  std::string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    out += lines[i];
    if (i + 1 < lines.size()) out += crlf ? "\r\n" : "\n";
  }
  return out;
}

}  // namespace

WriteResult applyEditsToFiles(const std::string &file,
                              const std::vector<TextEdit> &edits) {
  WriteResult r;
  if (edits.empty()) {
    r.ok = true;
    return r;  // 无 edit：不动文件（幂等重跑的自然结果）
  }

  // 按 e.file 分组（空归主文件），保持首次出现顺序
  std::vector<std::string> order;
  std::map<std::string, std::vector<const TextEdit *>> groups;
  for (const TextEdit &e : edits) {
    std::string f = e.file.empty() ? file : e.file;
    if (groups.find(f) == groups.end()) order.push_back(f);
    groups[f].push_back(&e);
  }

  for (const std::string &path : order) {
    // 读取
    std::string data;
    std::string ioErr;
    if (!readFileBytes(path, &data, &ioErr)) {
      r.error = ioErr;
      return r;
    }
    // BOM 保留（行号/列号都从 BOM 之后的内容起算——BOM 在第 1 行第 1 列
    // 之前的说法不成立：clang 的 spelling column 会把 BOM 计入列偏移，
    // 但我们的插入点均来自 decl/语句位置，按无 BOM 处理并在写出时回加 BOM
    // 与 VSCode applyEdit 行为一致）
    std::string bom;
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
      bom = data.substr(0, 3);
      data = data.substr(3);
    }

    // 行切分 + 降序应用
    std::vector<std::string> lines;
    bool crlf = false;
    splitLines(data, &lines, &crlf);

    // 拷贝出本组 edits 并按位置降序排序（位置大者先应用，行号不受
    // 先前插入影响）。同位置时保持引擎产出顺序的逆序，保证重复插入
    // 时后产出的在前面（与逐条 applyEdit 的最终布局一致）。
    std::vector<const TextEdit *> es = groups[path];
    std::stable_sort(es.begin(), es.end(),
                     [](const TextEdit *a, const TextEdit *b) {
                       if (a->start.line != b->start.line)
                         return a->start.line > b->start.line;
                       return a->start.col > b->start.col;
                     });

    for (const TextEdit *e : es) {
      int ln = e->start.line;
      // TextEdit 语义当前只有纯插入（start==end，见 text_edit.h 注释）。
      // 万一未来出现区间替换，这里保守拒绝而不是静默错误。
      if (!(e->start.line == e->end.line && e->start.col == e->end.col)) {
        r.error = "unsupported range-replace edit at " + path + ":" +
                  std::to_string(e->start.line);
        return r;
      }
      if (ln < 1 || static_cast<size_t>(ln) > lines.size()) {
        r.error = "edit position out of range: " + path + ":" +
                  std::to_string(ln);
        return r;
      }
      std::string &line = lines[static_cast<size_t>(ln) - 1];
      // col 1-based 字节列；col=行长+1 → 行尾。多字节字符中间的列对不齐
      // 字符边界时回退（与插件 byteColToUtf16 的兜底一致），避免劈开
      // UTF-8 序列。
      size_t col = static_cast<size_t>(e->start.col);
      if (col < 1) col = 1;
      if (col > line.size() + 1) col = line.size() + 1;
      while (col > 1 && static_cast<unsigned char>(line[col - 1]) >= 0x80 &&
             static_cast<unsigned char>(line[col - 1]) < 0xC0) {
        --col;  // 落在多字节字符中间 → 回退到字符起始字节
      }
      // 注入文本中的 \n 必须真正换行（等价于多条独立插入）：把该行按
      // 插入点切开，text 里的 \n 拆成多行，行尾风格跟随目标文件。
      std::string left = line.substr(0, col - 1);
      std::string right = line.substr(col - 1);
      const std::string &raw = e->text;
      std::vector<std::string> mid;
      {
        std::string cur;
        for (size_t i = 0; i < raw.size(); ++i) {
          char ch = raw[i];
          if (ch == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            mid.push_back(cur);
            cur.clear();
          } else {
            cur += ch;
          }
        }
        mid.push_back(cur);  // 末段（不带换行）
      }
      // 行数组内替换：line → left + mid[0], mid[1..n-2] …, mid[n-1] + right
      std::vector<std::string> replacement;
      replacement.push_back(left + mid.front());
      for (size_t i = 1; i + 1 < mid.size(); ++i) replacement.push_back(mid[i]);
      std::string tail = mid.back() + right;
      if (mid.size() == 1) {
        replacement.clear();
        replacement.push_back(left + mid.front() + right);
      } else {
        replacement.push_back(tail);
      }
      lines.erase(lines.begin() + (static_cast<size_t>(ln) - 1));
      lines.insert(lines.begin() + (static_cast<size_t>(ln) - 1),
                   replacement.begin(), replacement.end());
    }

    std::string out = bom + joinLines(lines, crlf);
    if (!writeFileBytes(path, out, &ioErr)) {
      r.error = ioErr;
      return r;
    }
    ++r.filesWritten;
  }
  r.ok = true;
  return r;
}

}  // namespace logc
