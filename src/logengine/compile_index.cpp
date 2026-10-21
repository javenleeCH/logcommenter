#include "compile_index.h"

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#include <algorithm>
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

// 从 JSON 字节流中定位所有 "file": "..." 条目，返回 (条目'{'偏移, 条目'}'偏移, 文件路径)
struct FileEntry {
  size_t start;  // 条目 '{'
  size_t end;    // 条目 '}'
  std::string path;
};

// 向前（start 方向）从 pos 起找最近的 '{'
size_t findBackwardBrace(const std::string &data, size_t pos) {
  if (pos == 0) return std::string::npos;
  // 向后找条目开括号 '{'，但必须跳过引号字符串（command 里可能含 { }）。
  // 否则一旦某条 command 字符串里有 '{'，会把条目起点选在字符串中间，导致括号失衡、跳过后续条目。
  bool in_str = false, esc = false;
  for (size_t i = pos; i-- > 0;) {
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
      return i;
    }
  }
  return std::string::npos;
}

// 向后从 start 起，跳过字符串，平衡括号到匹配的 '}'
size_t findForwardBalanced(const std::string &data, size_t start) {
  int depth = 0;
  bool in_str = false, esc = false;
  for (size_t i = start; i < data.size(); ++i) {
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
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0) return i;
    }
  }
  return std::string::npos;
}

std::vector<FileEntry> scanEntries(const std::string &data) {
  std::vector<FileEntry> out;
  const size_t n = data.size();
  size_t i = 0;
  while (i < n) {
    // 找到 "file" 键
    size_t hit = data.find("\"file\"", i);
    if (hit == std::string::npos) break;
    // 该键后面应有 ':' 和一个字符串值
    size_t j = hit + 6;
    while (j < n && (data[j] == ' ' || data[j] == '\t' ||
                     data[j] == '\r' || data[j] == '\n')) {
      ++j;
    }
    if (j >= n || data[j] != ':') {
      i = hit + 6;
      continue;
    }
    ++j;
    while (j < n && (data[j] == ' ' || data[j] == '\t' ||
                     data[j] == '\r' || data[j] == '\n')) {
      ++j;
    }
    if (j >= n || data[j] != '"') {
      i = hit + 6;
      continue;
    }
    // 读字符串值
    ++j;
    std::string path;
    bool esc = false;
    while (j < n) {
      char c = data[j];
      if (esc) {
        path += c;
        esc = false;
      } else if (c == '\\') {
        esc = true;
      } else if (c == '"') {
        break;
      } else {
        path += c;
      }
      ++j;
    }
    // 条目边界
    size_t start = findBackwardBrace(data, hit);
    size_t end = (start == std::string::npos)
                     ? std::string::npos
                     : findForwardBalanced(data, start);
    if (start != std::string::npos && end != std::string::npos) {
      out.push_back({start, end, path});
    }
    i = (end == std::string::npos) ? hit + 6 : end + 1;
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
    idx_[e.path] = {e.start, e.end - e.start + 1};
  }
  jsonPath_ = jsonPath;
  return true;
}

bool CompileIndex::locate(const std::string &file, size_t *offset,
                          size_t *length) const {
  auto it = idx_.find(file);
  if (it == idx_.end()) return false;
  if (offset) *offset = it->second.first;
  if (length) *length = it->second.second;
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
      return readAt(it->second.first, it->second.second, entryJson);
    }
  }
  for (auto &kv : idx_) {
    if (normalizeSlashes(kv.first) == nfile) {
      return readAt(kv.second.first, kv.second.second, entryJson);
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
      std::string kabs = absolutePath(kv.first);
      if (!kabs.empty() && kabs == abs) {
        return readAt(kv.second.first, kv.second.second, entryJson);
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
  for (auto &kv : idx_) {
    f << "{\"f\":\"" << kv.first << "\",\"o\":" << kv.second.first
      << ",\"l\":" << kv.second.second << "}\n";
  }
  return f.good();
}

bool CompileIndex::loadCache(const std::string &cachePath,
                             const std::string &jsonPath) {
  std::ifstream f(cachePath);
  if (!f) return false;
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
    if (file.empty() || o < 0 || l < 0) continue;
    idx_[file] = {static_cast<size_t>(o), static_cast<size_t>(l)};
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
