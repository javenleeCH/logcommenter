#include "compile_index.h"

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
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

// 流式扫描 compile_commands.json：分块读取，内存占用 = 固定缓冲区 + 当前
// 条目的 file 路径/键（与文件大小无关）。规划 §3.2 要求“一次性流式扫描 1GB，
// 只记录偏移，内存可忽略”，因此绝不允许把整份文件读进内存再扫描。
//
// 正向扫描：compile_commands.json 是一个顶层数组 [ {..}, {..} ]，
// 每个条目是无嵌套对象（键均为 directory/command/file/output 等字符串）。
// 严格按“引号状态 + 转义 + 括号深度”单向扫描，定位每个顶层对象的 [start,end]，
// 同时在条目内捕获 "file" 值（反转义后的路径）与 "directory" 值原文区间。
//
// 必须正向（不能反向找 '{'）：Windows 的 MSVC 命令以转义引号开头，例如
//   "command": "\"C:\\Program Files\\...\\cl.exe\" ..."
// 反向扫描无法可靠配对 '\\' 与 '\"'，会把字符串真正的开引号误判为转义引号、
// 造成括号错位而丢条目。正向转义处理则没有该问题。
//
// 键识别：字符串外记录最近一个非空白字符 prevSig；在条目层（depth==1）：
//   '{' 或 ',' 之后的字符串是键；':' 之后的字符串是值。
// 值只捕获 "file"（路径，需反转义）与 "directory"（只记原文偏移，按需读取）。
constexpr size_t kChunkBytes = 256 * 1024;

bool scanEntries(const std::string &path, std::vector<FileEntry> *out,
                 std::string *err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (err) *err = "cannot open: " + path;
    return false;
  }

  std::vector<char> buf(kChunkBytes);
  size_t base = 0;  // 当前块首字节的全局偏移

  bool in_str = false, esc = false;
  int depth = 0;
  char prev_sig = 0;  // 字符串外最近一个非空白字符
  size_t entry_start = 0;
  bool has_file = false;
  std::size_t dir_off = 0, dir_len = 0;
  std::string file_path, file_buf, pending_key, key_buf;
  bool capturing_key = false;

  // 值捕获状态：file 需累积反转义内容；directory 只记原文区间。
  enum Cap { kNone, kFile, kDir } cap = kNone;
  size_t val_start = 0;

  auto resetEntry = [&]() {
    has_file = false;
    dir_off = dir_len = 0;
    file_path.clear();
    file_buf.clear();
    pending_key.clear();
    key_buf.clear();
    capturing_key = false;
    cap = kNone;
  };

  for (;;) {
    f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    std::streamsize got = f.gcount();
    if (got <= 0) break;
    for (std::streamsize k = 0; k < got; ++k) {
      char c = buf[static_cast<size_t>(k)];
      const size_t abs = base + static_cast<size_t>(k);
      if (in_str) {
        if (esc) {
          esc = false;
          if (cap == kFile) {
            // JSON 转义还原：Windows 路径/命令里的反斜杠、转义引号需要原样保留
            if (c == 'n') file_buf += '\n';
            else if (c == 't') file_buf += '\t';
            else if (c == 'r') file_buf += '\r';
            else file_buf += c;  // \" \\ \/ 等原样保留后续字符
          }
        } else if (c == '\\') {
          esc = true;
        } else if (c == '"') {
          in_str = false;
          prev_sig = '"';
          if (capturing_key) {
            pending_key = std::move(key_buf);
            key_buf.clear();
            capturing_key = false;
          } else if (cap == kFile) {
            file_path = std::move(file_buf);
            file_buf.clear();
            has_file = true;
            cap = kNone;
          } else if (cap == kDir) {
            dir_off = val_start;
            dir_len = abs - val_start;  // 原文长度（转义序列按原样计数）
            cap = kNone;
          }
        } else if (capturing_key) {
          key_buf += c;
        } else if (cap == kFile) {
          file_buf += c;
        }
        continue;
      }
      // ---- 字符串外 ----
      if (c == '"') {
        in_str = true;
        esc = false;
        if (depth == 1 && prev_sig == ':') {
          if (pending_key == "file") {
            cap = kFile;
            val_start = abs + 1;
            file_buf.clear();
          } else if (pending_key == "directory") {
            cap = kDir;
            val_start = abs + 1;
          } else {
            cap = kNone;
          }
          capturing_key = false;
        } else if (depth == 1 && (prev_sig == '{' || prev_sig == ',')) {
          capturing_key = true;
          cap = kNone;
          key_buf.clear();
        } else {
          capturing_key = false;
          cap = kNone;
        }
        prev_sig = '"';
        continue;
      }
      if (c == '{') {
        if (depth == 0) {
          entry_start = abs;
          resetEntry();
        }
        ++depth;
        prev_sig = '{';
      } else if (c == '}') {
        if (depth > 0) {
          --depth;
          if (depth == 0 && has_file) {
            // 条目区间含闭合 '}'（build 里 length = end - start + 1）
            out->push_back(FileEntry{entry_start, abs, std::move(file_path),
                                     dir_off, dir_len});
            resetEntry();
          }
        }
        prev_sig = '}';
      } else if (!std::isspace(static_cast<unsigned char>(c))) {
        prev_sig = c;
      }
    }
    base += static_cast<size_t>(got);
    // 短读即到文件尾；恰好整块时下一轮 gcount==0 退出。
    if (got < static_cast<std::streamsize>(buf.size())) break;
  }
  if (f.bad()) {
    if (err) *err = "read failed: " + path;
    return false;
  }
  return true;
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

// 比较专用规范化：先统一斜杠；Windows 盘符与路径段一并转小写（NTFS 不区分大小写）。
// 背景：VSCode 的 document.uri.fsPath 返回小写盘符（d:\...），而 compile_commands 条目
// 常存大写盘符（D:/...）；GetFullPathName/_fullpath 保留输入盘符原始大小写，不折叠，
// 只统一斜杠后字符串比较仍不等，导致插件路径失配（命令行传相对路径走绝对化兜底才命中）。
std::string normalizeCompare(std::string p) {
  p = normalizeSlashes(std::move(p));
#ifdef _WIN32
  for (auto &c : p) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
#endif
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
  // 分块流式扫描：不把整份文件读进内存（首次建索引内存峰值与文件大小解耦）
  std::vector<FileEntry> entries;
  if (!scanEntries(jsonPath, &entries, err)) return false;

  // 记录 mtime + size（isStale 双检：mtime 秒级精度下同秒重写靠 size 兜住）
  struct stat st;
  if (stat(jsonPath.c_str(), &st) == 0) {
    mtime_ = st.st_mtime;
    size_ = static_cast<long long>(st.st_size);
  }

  idx_.clear();
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
  const std::string nfile = normalizeCompare(file);

  // 第一遍：斜杠 +（Windows）大小写归一后遍历比较。覆盖：精确相等、斜杠方向差异、
  // 盘符/路径段大小写差异（VSCode fsPath 小写盘符 vs compile_commands 大写盘符）。
  for (auto &kv : idx_) {
    if (normalizeCompare(kv.first) == nfile) {
      return readAt(kv.second.offset, kv.second.length, entryJson);
    }
  }

  // 第二遍兜底：compile_commands 里是绝对路径而用户传相对路径时，先把用户路径绝对化，
  // 再与"条目绝对路径"或"directory 拼接相对 file"归一后的结果比较。
  std::string abs = absolutePath(file);
  if (abs.empty()) return false;
  const std::string nabs = normalizeCompare(abs);
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
    if (!kabs.empty() && normalizeCompare(kabs) == nabs) {
      return readAt(kv.second.offset, kv.second.length, entryJson);
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
  // v3：键 JSON 转义修复（v2 未转义反斜杠路径，往返损坏）+ #src 状态行
  // （json 的 msize/mtime 快照，供 isStale 双检）。旧缓存不兼容，loadCache
  // 返回 false 由调用方重建。
  f << "#logc-idx-v3\n";
  f << "#src " << size_ << " " << mtime_ << "\n";
  for (auto &kv : idx_) {
    // 键含反斜杠（Windows 路径）或引号时必须转义：loadCache 按 JSON 转义解析，
    // 不转义则 "\x" 被当转义序列、反斜杠被吞，缓存往返后 key 损坏、查找失败。
    std::string esc;
    esc.reserve(kv.first.size() + 8);
    for (char c : kv.first) {
      if (c == '\\' || c == '"') esc += '\\';
      esc += c;
    }
    f << "{\"f\":\"" << esc << "\",\"o\":" << kv.second.offset
      << ",\"l\":" << kv.second.length << ",\"d\":" << kv.second.dirOff
      << ",\"dl\":" << kv.second.dirLen << "}\n";
  }
  return f.good();
}

bool CompileIndex::loadCache(const std::string &cachePath,
                             const std::string &jsonPath) {
  std::ifstream f(cachePath);
  if (!f) return false;
  // 版本检测 + #src 状态行（json 的 size/mtime 快照）：旧格式或缺失时返回
  // false，调用方走 build 重建。
  std::string firstLine;
  if (!std::getline(f, firstLine) || firstLine != "#logc-idx-v3")
    return false;
  std::string srcLine;
  if (!std::getline(f, srcLine) || srcLine.rfind("#src ", 0) != 0)
    return false;
  std::istringstream is(srcLine.substr(5));
  if (!(is >> size_ >> mtime_)) return false;
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
  return true;
}

std::time_t CompileIndex::sourceMtime() const {
  struct stat st;
  if (stat(jsonPath_.c_str(), &st) == 0) return st.st_mtime;
  return 0;
}

long long CompileIndex::sourceSize() const {
  struct stat st;
  if (stat(jsonPath_.c_str(), &st) == 0)
    return static_cast<long long>(st.st_size);
  return -1;
}

bool CompileIndex::isStale() const {
  // mtime + size 双检：mtime 只有秒级精度，同秒内重写 json（改内容）靠
  // size 变化兜住，避免用旧 offset 读错条目。
  std::time_t cur = sourceMtime();
  if (cur == 0) return false;
  if (cur != mtime_) return true;
  return sourceSize() != size_;
}

}  // namespace logc
