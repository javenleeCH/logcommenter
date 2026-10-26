#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "test_framework.h"
#include "logengine/compile_index.h"

using namespace logc;

namespace {

// 写一个小的 compile_commands.json 到临时路径，返回其路径
std::string makeFixture() {
  char buf[L_tmpnam];
  const char *name = std::tmpnam(buf);
  std::string p = name ? std::string(name) + ".json" : "/tmp/logc_fixture.json";
  std::ofstream f(p, std::ios::trunc);
  f << "[\n"
    << "  {\"directory\":\"/proj\",\"file\":\"src/foo.cpp\",\"command\":\"g++ -c foo.cpp -I inc\"},\n"
    << "  {\"directory\":\"/proj\",\"file\":\"src/bar.cpp\",\"command\":\"g++ -c bar.cpp -DDEBUG\"},\n"
    << "  {\"directory\":\"/proj\",\"file\":\"lib/other.cpp\",\"command\":\"g++ -c other.cpp\"}\n"
    << "]\n";
  f.close();
  return p;
}

void cleanup(const std::string &p) {
  std::remove(p.c_str());
  std::remove((p + ".logc-idx").c_str());
}

// 跨平台临时目录：Windows 用 %TEMP%，Unix 用 /tmp。
// 归一为正斜杠——Windows TEMP 的反斜杠会破坏下面 JSON fixture 的合法性，
// 而文件系统（ofstream）在 Windows 上同样接受正斜杠。
std::string tmpDir() {
  const char *td = std::getenv("TMPDIR");
  if (!td || !*td) td = std::getenv("TEMP");
  if (!td || !*td) td = "/tmp";
  std::string d = td;
  for (char &c : d)
    if (c == '\\') c = '/';
  return d;
}

}  // namespace

TEST_CASE(index_locate_and_read_entry) {
  std::string p = makeFixture();
  CompileIndex idx;
  std::string err;
  EXPECT_TRUE(idx.build(p, &err));
  EXPECT_EQ(idx.size(), 3u);

  size_t off = 0, len = 0;
  EXPECT_TRUE(idx.locate("src/foo.cpp", &off, &len));
  EXPECT_TRUE(len > 0);

  std::string entry;
  EXPECT_TRUE(idx.readEntry("src/bar.cpp", &entry));
  EXPECT_CONTAINS(entry, "-DDEBUG");
  EXPECT_CONTAINS(entry, "src/bar.cpp");

  EXPECT_FALSE(idx.locate("src/nope.cpp", &off, &len));
  cleanup(p);
}

TEST_CASE(index_cache_roundtrip) {
  std::string p = makeFixture();
  std::string cache = p + ".logc-idx";
  CompileIndex idx;
  std::string err;
  EXPECT_TRUE(idx.build(p, &err));
  EXPECT_TRUE(idx.saveCache(cache));

  CompileIndex idx2;
  EXPECT_TRUE(idx2.loadCache(cache, p));
  EXPECT_EQ(idx2.size(), idx.size());
  size_t off = 0, len = 0;
  EXPECT_TRUE(idx2.locate("lib/other.cpp", &off, &len));
  EXPECT_FALSE(idx2.isStale());
  // 缓存读取后 readEntry 必须返回与 build 一致的原始条目（数字字段 o/l 不能丢）
  std::string e2;
  EXPECT_TRUE(idx2.readEntry("src/bar.cpp", &e2));
  EXPECT_CONTAINS(e2, "-DDEBUG");
  EXPECT_CONTAINS(e2, "src/bar.cpp");

  // 改动原文件后应判定过期（mtime 秒级粒度，先等待跨过当前秒）
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  std::ofstream f(p, std::ios::app);
  f << "\n";
  f.close();
  EXPECT_TRUE(idx2.isStale());
  cleanup(p);
}

TEST_CASE(index_read_normalized_abs_path) {
  // compile_commands 里的 file 带 "./" 等非规范化写法，readEntry 传规范绝对路径
  // 应通过 realpath 兜底命中（不依赖精确字符串匹配）。
  std::string dir = tmpDir();
  std::string tmp = dir + "/logc_norm_src.cpp";
  {
    std::ofstream f(tmp);
    f << "int main() { return 0; }\n";
  }
  std::string db = dir + "/logc_norm_db.json";
  {
    std::ofstream f(db);
    f << "[\n"
      << "  {\"directory\":\"" << dir << "\",\"file\":\"" << dir << "/./logc_norm_src.cpp\","
      << "\"command\":\"g++ -c logc_norm_src.cpp\"}\n"
      << "]\n";
  }
  CompileIndex idx;
  std::string err;
  EXPECT_TRUE(idx.build(db, &err));
  // 规范绝对路径与索引键（带 ./）字符串不同，但 realpath 归一后相同
  std::string entry;
  EXPECT_TRUE(idx.readEntry(tmp, &entry));
  EXPECT_CONTAINS(entry, "logc_norm_src.cpp");
  std::remove(tmp.c_str());
  std::remove(db.c_str());
}

TEST_CASE(index_read_slash_normalize_windows_path) {
  // Windows 场景：compile_commands 存反斜杠绝对路径，用户传正斜杠绝对路径。
  // 应通过斜杠归一直接字符串匹配命中（不依赖 realpath，路径在 Linux 上不存在也没关系）。
  std::string db = tmpDir() + "/logc_win_db.json";
  {
    std::ofstream f(db);
    f << "[\n"
      << "  {\"directory\":\"D:\\\\codeSpace\\\\CDir\\\\vkdemo\","
      << "\"file\":\"D:\\\\codeSpace\\\\CDir\\\\vkdemo\\\\src\\\\main.cpp\","
      << "\"command\":\"cl /c src\\\\main.cpp\"}\n"
      << "]\n";
  }
  CompileIndex idx;
  std::string err;
  EXPECT_TRUE(idx.build(db, &err));
  std::string entry;
  // 正斜杠绝对路径 → 应命中反斜杠存储的键
  EXPECT_TRUE(idx.readEntry("D:/codeSpace/CDir/vkdemo/src/main.cpp", &entry));
  EXPECT_CONTAINS(entry, "main.cpp");
  std::remove(db.c_str());
}

#ifdef _WIN32
TEST_CASE(index_read_case_insensitive_windows_drive) {
  // 真实插件形态：VSCode document.uri.fsPath 返回小写盘符 + 反斜杠
  // （d:\codespace\...），而 compile_commands 存大写盘符 + 正斜杠（D:/...）。
  // NTFS 不区分大小写，比较时必须折叠盘符与路径段大小写；不依赖文件存在。
  std::string db = tmpDir() + "/logc_case_db.json";
  {
    std::ofstream f(db);
    f << "[\n"
      << "  {\"directory\":\"D:/codespace/CDir/vkdemo\","
      << "\"file\":\"D:/codespace/CDir/vkdemo/src/main.cpp\","
      << "\"command\":\"cl /c src/main.cpp\"}\n"
      << "]\n";
  }
  CompileIndex idx;
  std::string err;
  EXPECT_TRUE(idx.build(db, &err));
  std::string entry;
  // 小写盘符 + 反斜杠（插件 fsPath 形态）→ 应命中大写盘符 + 正斜杠的键
  EXPECT_TRUE(idx.readEntry("d:\\codespace\\CDir\\vkdemo\\src\\main.cpp", &entry));
  EXPECT_CONTAINS(entry, "main.cpp");
  // 路径段大小写混用（codespace vs CodeSpace）同样应命中
  EXPECT_TRUE(idx.readEntry("D:/CodeSpace/CDir/vkdemo/src/main.cpp", &entry));
  EXPECT_CONTAINS(entry, "main.cpp");
  std::remove(db.c_str());
}
#endif

TEST_CASE(index_read_relative_file_via_directory) {
  // 真实 monorepo（鸿蒙等）：file 是相对路径，基准是该条目的 directory 字段，
  // 不是当前工作目录。用户（VSCode / CLI）传真实绝对路径时应命中。
  namespace fs = std::filesystem;
  std::string base =
      tmpDir() + "/logc_relproj_" +
      std::to_string(std::chrono::system_clock::now()
                         .time_since_epoch()
                         .count());
  std::string out = base + "/out";                 // compile_commands 的 directory
  std::string srcDir = base + "/foundation/src";   // 相对 out 向上两级
  std::string src = srcDir + "/a.cpp";
  fs::create_directories(out);
  fs::create_directories(srcDir);
  {
    std::ofstream f(src);
    f << "int a = 1;\n";
  }
  std::string db = base + "/db.json";
  {
    std::ofstream f(db);
    f << "[\n"
      << "  {\"directory\":\"" << out << "\","
      << "\"file\":\"../foundation/src/a.cpp\","
      << "\"command\":\"g++ -c ../foundation/src/a.cpp\"}\n"
      << "]\n";
  }
  CompileIndex idx;
  std::string err;
  EXPECT_TRUE(idx.build(db, &err));

  std::string entry;
  // 1) 绝对路径 → 经 directory 拼接 + realpath 命中（相对 file 场景）
  EXPECT_TRUE(idx.readEntry(src, &entry));
  EXPECT_CONTAINS(entry, "a.cpp");
  // 2) 相对路径原样 → 精确命中
  EXPECT_TRUE(idx.readEntry("../foundation/src/a.cpp", &entry));
  EXPECT_CONTAINS(entry, "a.cpp");

  // 3) 缓存往返后 d/dl 字段保留，绝对路径仍能命中
  std::string cache = db + ".logc-idx";
  EXPECT_TRUE(idx.saveCache(cache));
  CompileIndex idx2;
  EXPECT_TRUE(idx2.loadCache(cache, db));
  EXPECT_TRUE(idx2.readEntry(src, &entry));
  EXPECT_CONTAINS(entry, "a.cpp");

  fs::remove_all(base);
}

TEST_CASE(index_cache_v1_rejected) {
  // 旧版缓存（无 #logc-idx-v2 首行标记）应被拒绝，由调用方重建。
  std::string p = makeFixture();
  std::string cache = p + ".logc-idx";
  {
    std::ofstream f(cache);
    f << "{\"f\":\"src/foo.cpp\",\"o\":10,\"l\":50}\n";
  }
  CompileIndex idx;
  EXPECT_FALSE(idx.loadCache(cache, p));
  // 重建后 v2 缓存可用
  std::string err;
  EXPECT_TRUE(idx.build(p, &err));
  EXPECT_TRUE(idx.saveCache(cache));
  CompileIndex idx2;
  EXPECT_TRUE(idx2.loadCache(cache, p));
  EXPECT_EQ(idx2.size(), idx.size());
  cleanup(p);
}
