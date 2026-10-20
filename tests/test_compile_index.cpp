#include <cstdio>
#include <cstdlib>
#include <chrono>
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
  std::string tmp = "/tmp/logc_norm_src.cpp";
  {
    std::ofstream f(tmp);
    f << "int main() { return 0; }\n";
  }
  std::string db = "/tmp/logc_norm_db.json";
  {
    std::ofstream f(db);
    f << "[\n"
      << "  {\"directory\":\"/tmp\",\"file\":\"/tmp/./logc_norm_src.cpp\","
      << "\"command\":\"g++ -c logc_norm_src.cpp\"}\n"
      << "]\n";
  }
  CompileIndex idx;
  std::string err;
  EXPECT_TRUE(idx.build(db, &err));
  // 规范绝对路径与索引键（带 ./）字符串不同，但 realpath 归一后相同
  std::string entry;
  EXPECT_TRUE(idx.readEntry("/tmp/logc_norm_src.cpp", &entry));
  EXPECT_CONTAINS(entry, "logc_norm_src.cpp");
  std::remove(tmp.c_str());
  std::remove(db.c_str());
}
