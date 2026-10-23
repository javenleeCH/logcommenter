#include "compile_index.h"

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <stdlib.h>  // _fullpath
#else
#include <limits.h>  // PATH_MAX
#include <stdlib.h>  // realpath
#endif

namespace logc {

namespace {

// 从 JSON 字节流中定位所有 compile_commands 条目，返回条目区间、file 路径，
// 以及 "directory" 值内容在原文中的区间（未反转义，按需读取）。
struct FileEntry {
  size_t start;  // 条目 '{'
  size_t end;    // 条目 '}'
  std::string path;
  size_t dirOff = 0;
  size_t dirLen = 0;
};

// 从 ':' 后第一个引号开始读取 JSON 字符串字面量，正确处理 \" \\ 等转义。
// 返回读取结束位置（闭合引号位置），失败返回 npos。
bool readJsonString(const std::string &data, size_t pos, size_t limit,
                    std::string *out, size_t *closePos) {
  size_t j = pos;
  while (j < limit && (data[j] == ' ' || data[j] == '\t' ||
                       data[j] == '\r' || data[j] == '\n')) {
    ++j;
  }
  if (j >= limit || data[j] != '"') return false;
  ++j;
  std::string s;
  bool esc = false;
  while (j < limit) {
    char c = data[j];
    if (esc) {
      // JSON 转义还原：Windows 路径/命令里的反斜杠、转义引号需要原样保留
      if (c == 'n') s += '\n';
      else if (c == 't') s += '\t';
      else if (c == 'r') s += '\r';
      else s += c;  // \" \\ \/ 等原样保留后续字符
      esc = false;
    } else if (c == '\\') {
      esc = true;
    } else if (c == '"') {
      *out = std::move(s);
      if (closePos) *closePos = j;
      return true;
    } else {
      s += c;
    }
    ++j;
  }
  return false;
}

// 正向扫描：compile_commands.json 是一个顶层数组 [ {..}, {..} ]，
// 每个条目是一个无嵌套对象（键均为 directory/command/file/output 等字符串）。
// 严格按“引号状态 + 转义 + 括号深度”单向扫描，定位每个顶层对象的 [start,end]，
// 再在区间内提取 \"file\" 值。
//
// 必须正向（不能反向找 '{'）：Windows 的 MSVC 命令以转义引号开头，例如
//   "command": "\"C:\\Program Files\\...\\cl.exe\" ..."
// 反向扫描无法可靠配对 '\\' 与 '\"'，会把字符串真正的开引号误判为转义引号、
// 造成括号错位而丢条目。正向转义处理则没有该问题。
std::vector<FileEntry> scanEntries(const std::string &data) {
  std::vector<FileEntry> out;
  const size_t n = data.size();
  bool in_str = false, esc = false;
  int depth = 0;
  size_t start = std::string::npos;

  for (size_t i = 0; i < n; ++i) {
    char c = data[i];
    if (in_str) {
      if (esc) {
        esc = false;
      } else if (c == '\\') {
        esc = true;
      } else if (c == '"') {
        in_str = false;
      }
      continue;
    }
    if (c == '"') {
      in_str = true;
    } else if (c == '{') {
      if (depth == 0) start = i;
      ++depth;
    } else if (c == '}') {
      if (depth > 0) {
        --depth;
        if (depth == 0 && start != std::string::npos) {
          // 在 [start, i] 区间内提取 \"file\" 字符串
          size_t hit = data.find("\"file\"", start + 1);
          if (hit != std::string::npos && hit < i) {
            size_t k = hit + 6;
            while (k < i && (data[k] == ' ' || data[k] == '\t' ||
                             data[k] == '\r' || data[k] == '\n')) {
              ++k;
            }
            std::string path;
            if (k < i && data[k] == ':') {
              size_t closePos;
              if (readJsonString(data, k + 1, i, &path, &closePos)) {
                FileEntry fe;
                fe.start = start;
                fe.end = i;
                fe.path = std::move(path);
                // 提取 "directory" 值内容区间（原文、未反转义；dirLen=0 表示缺失）。
                // 键在条目内只出现一次，与 file 的相对顺序无关。
                size_t dhit = data.find("\"directory\"", start + 1);
                if (dhit != std::string::npos && dhit < i) {
                  size_t dk = dhit + 11;  // 键 "directory" 长度（含引号）
                  while (dk < i && (data[dk] == ' ' || data[dk] == '\t' ||
                                    data[dk] == '\r' || data[dk] == '\n')) {
                    ++dk;
                  }
                  if (dk < i && data[dk] == ':') {
                    size_t dj = dk + 1;
                    while (dj < i && (data[dj] == ' ' || data[dj] == '\t' ||
                                      data[dj] == '\r' || data[dj] == '\n')) {
                      ++dj;
                    }
                    if (dj < i && data[dj] == '"') {
                      size_t cs = dj + 1;  // 内容起始（开引号之后）
                      size_t e = cs;
                      bool esc = false;
                      while (e < i) {
                        char c = data[e];
                        if (esc) {
                          esc = false;
                        } else if (c == '\\') {
                          esc = true;
                        } else if (c == '"') {
                          break;
                        }
                        ++e;
                      }
                      if (e < i && e > cs) {
                        fe.dirOff = cs;
                        fe.dirLen = e - cs;
                      }
                    }
                  }
                }
                out.push_back(std::move(fe));
              }
            }
          }
          start = std::string::npos;
        }
      }
    }
  }
  return out;
}

bool readFileBytes(const std::string &path, std::string *data,
                   std::string *err) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    if (err) *err = "cannot open: " + path;
    return false;
  }
  std::streamsize sz = f.tellg();
  if (sz < 0) {
    if (err) *err = "cannot stat: " + path;
    return false;
  }
  data->resize(static_cast<size_t>(sz));
  f.seekg(0, std::ios::beg);
  f.read(&(*data)[0], sz);
  return f.good();
}

// 把路径分隔符统一为 '/'，便于跨平台比较：
// Windows 上 _fullpath 返回反斜杠，而 compile_commands.json 常为正斜杠，
// 归一后两者才能精确比较。
std::string normalizeSlashes(std::string p) {
  for (auto &c : p) {
    if (c == '\\') c = '/';
  }
  return p;
}

// 把路径解析为绝对路径（基于当前工作目录），失败返回空串。
// 用于把用户传入的相对路径与 compile_commands 中的绝对路径对齐。
// 返回前归一为 '/' 分隔符。
std::string absolutePath(const std::string &p) {
  std::string out;
#ifdef _WIN32
  char buf[_MAX_PATH];
  if (_fullpath(buf, p.c_str(), _MAX_PATH)) out = buf;
#else
  char buf[PATH_MAX];
  if (realpath(p.c_str(), buf)) out = buf;
#endif
  return normalizeSlashes(out);
}

// 判断路径是否绝对：Unix 以 '/' 开头；Windows 还认盘符 'X:'。
bool isAbsolutePath(const std::string &p) {
  if (!p.empty() && (p[0] == '/' || p[0] == '\\')) return true;
#ifdef _WIN32
  if (p.size() >= 2 &&
      std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':')
    return true;
#endif
  return false;
}

// 拼接 directory 与相对 file（补一个分隔符）。
std::string joinPath(const std::string &dir, const std::string &rel) {
  std::string out = dir;
  if (!out.empty() && out.back() != '/' && out.back() != '\\') out += '/';
  out += rel;
  return out;
}

// JSON 字符串转义还原（用于按需读取的 directory 原文）。
std::string unescapeJson(const std::string &s) {
  std::string out;
  bool esc = false;
  for (char c : s) {
    if (esc) {
      if (c == 'n') out += '\n';
      else if (c == 't') out += '\t';
      else if (c == 'r') out += '\r';
      else out += c;  // \" \\ \/ 等
      esc = false;
    } else if (c == '\\') {
      esc = true;
    } else {
      out += c;
    }
  }
  return out;
}

}  // namespace

bool CompileIndex::build(const std::string &jsonPath, std::string *err) {
  std::string data;
  if (!readFileBytes(jsonPath, &data, err)) return false;

  // 记录 mtime
  struct stat st;
  if (stat(jsonPath.c_str(), &st) == 0) mtime_ = st.st_mtime;

  idx_.clear();
  auto entries = scanEntries(data);
  for (auto &e : entries) {
    idx_[e.path] = {e.start, e.end - e.start + 1, e.dirOff, e.dirLen};
  }
  jsonPath_ = jsonPath;
  return true;
}

bool CompileIndex::locate(const std::string &file, size_t *offset,
                          size_t *length) const {
  auto it = idx_.find(file);
  if (it == idx_.end()) return false;
  if (offset) *offset = it->second.offset;
  if (length) *length = it->second.length;
  return true;
}

bool CompileIndex::readEntry(const std::string &file,
                             std::string *entryJson) const {
  size_t off, len;
  if (locate(file, &off, &len)) {
    return readAt(off, len, entryJson);
  }
  // 斜杠归一后直接比较：覆盖 "用户传正斜杠 vs compile_commands 存反斜杠" 的纯字符串
  // 差异，且不依赖文件系统存在性（_fullpath/realpath 可能受 cwd/存在性影响）。
  std::string nfile = normalizeSlashes(file);
  if (nfile != file) {
    auto it = idx_.find(nfile);
    if (it != idx_.end()) {
      return readAt(it->second.offset, it->second.length, entryJson);
    }
  }
  for (auto &kv : idx_) {
    if (normalizeSlashes(kv.first) == nfile) {
      return readAt(kv.second.offset, kv.second.length, entryJson);
    }
  }
  // 兜底：compile_commands 里通常是绝对路径，用户可能传相对路径。
  // 先试"相对→绝对"直接定位，再遍历索引比较规范化后的绝对路径。
  std::string abs = absolutePath(file);
  if (!abs.empty() && abs != file && locate(abs, &off, &len)) {
    return readAt(off, len, entryJson);
  }
  if (!abs.empty()) {
    for (auto &kv : idx_) {
      std::string kabs;
      if (isAbsolutePath(kv.first)) {
        kabs = absolutePath(kv.first);
      } else if (kv.second.dirLen > 0) {
        // 条目 file 是相对路径：基准是该条目的 directory（真实 monorepo 常见，
        // 如鸿蒙 "file":"../../../foundation/..." + "directory":"/srv/workspace/..."），
        // 不是当前工作目录。按需读 directory 原文后反转义，再拼接绝对化比较。
        std::string dirRaw;
        if (readAt(kv.second.dirOff, kv.second.dirLen, &dirRaw)) {
          std::string dir = unescapeJson(dirRaw);
          if (!dir.empty() && isAbsolutePath(dir))
            kabs = absolutePath(joinPath(dir, kv.first));
        }
      }
      if (!kabs.empty() && kabs == abs) {
        return readAt(kv.second.offset, kv.second.length, entryJson);
      }
    }
  }
  return false;
}

bool CompileIndex::readAt(size_t off, size_t len,
                          std::string *entryJson) const {
  std::ifstream f(jsonPath_, std::ios::binary);
  if (!f) return false;
  f.seekg(static_cast<std::streamoff>(off));
  entryJson->resize(len);
  f.read(&(*entryJson)[0], static_cast<std::streamsize>(len));
  return f.good();
}

bool CompileIndex::saveCache(const std::string &cachePath) const {
  std::ofstream f(cachePath, std::ios::trunc);
  if (!f) return false;
  // v2：首行版本标记，条目新增 d/dl（directory 内容区间），旧缓存不兼容时
  // loadCache 返回 false 由调用方重建，避免静默用缺 directory 的旧索引。
  f << "#logc-idx-v2\n";
  for (auto &kv : idx_) {
    f << "{\"f\":\"" << kv.first << "\",\"o\":" << kv.second.offset
      << ",\"l\":" << kv.second.length << ",\"d\":" << kv.second.dirOff
      << ",\"dl\":" << kv.second.dirLen << "}\n";
  }
  return f.good();
}

bool CompileIndex::loadCache(const std::string &cachePath,
                             const std::string &jsonPath) {
  std::ifstream f(cachePath);
  if (!f) return false;
  // 版本检测：旧格式（无首行标记 / 无 d/dl）→ 返回 false，调用方走 build 重建。
  std::string firstLine;
  if (!std::getline(f, firstLine) || firstLine != "#logc-idx-v2")
    return false;
  idx_.clear();
  std::string line;
  while (std::getline(f, line)) {
    // 解析 {"f":"...","o":123,"l":456}；file 用 JSON 转义
    auto val = [&](const std::string &key) -> std::string {
      size_t k = line.find("\"" + key + "\":");
      if (k == std::string::npos) return "";
      size_t v = line.find('"', k + key.size() + 3);
      if (v == std::string::npos) return "";
      ++v;
      std::string s;
      bool esc = false;
      while (v < line.size()) {
        char c = line[v];
        if (esc) {
          if (c == 'n') s += '\n';
          else if (c == 't') s += '\t';
          else if (c == 'r') s += '\r';
          else if (c == '"') s += '"';
          else if (c == '\\') s += '\\';
          else s += c;
          esc = false;
        } else if (c == '\\') {
          esc = true;
        } else if (c == '"') {
          return s;
        } else {
          s += c;
        }
        ++v;
      }
      return s;
    };
    std::string file = val("f");
    // o/l 是数字字段（"o":123），不能用字符串解析，直接从 `"key":` 后读整数
    auto num = [&](const std::string &key) -> long long {
      size_t k = line.find("\"" + key + "\":");
      if (k == std::string::npos) return -1;
      size_t v = k + key.size() + 3;
      while (v < line.size() &&
             (line[v] == ' ' || line[v] == '\t' || line[v] == '\r')) {
        ++v;
      }
      if (v >= line.size()) return -1;
      return std::strtoll(line.c_str() + v, nullptr, 10);
    };
    long long o = num("o"), l = num("l");
    long long d = num("d"), dl = num("dl");
    if (file.empty() || o < 0 || l < 0) continue;
    Entry e;
    e.offset = static_cast<size_t>(o);
    e.length = static_cast<size_t>(l);
    if (d > 0) e.dirOff = static_cast<size_t>(d);
    if (dl > 0) e.dirLen = static_cast<size_t>(dl);
    idx_[file] = e;
  }
  jsonPath_ = jsonPath;
  // isStale() 用 mtime_ 判断"json 是否比缓存新"。这里必须记缓存文件(生成时刻)的
  // mtime；若记加载时 json 的 mtime，则 isStale() 里 cur==mtime_ 恒为 false，旧缓存永不失效。
  struct stat st;
  if (stat(cachePath.c_str(), &st) == 0) mtime_ = st.st_mtime;
  return true;
}

std::time_t CompileIndex::sourceMtime() const {
  struct stat st;
  if (stat(jsonPath_.c_str(), &st) == 0) return st.st_mtime;
  return 0;
}

bool CompileIndex::isStale() const {
  std::time_t cur = sourceMtime();
  return cur != 0 && cur != mtime_;
}

}  // namespace logc
