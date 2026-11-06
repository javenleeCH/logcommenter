// file_apply 单测：--write 的纯逻辑部分（TextEdit → 文件字节）。
// 用临时文件验证：纯插入语义（1-based 字节列、行尾 col）、降序应用、
// CRLF 保持、BOM 保持、跨文件 e.file、无 edit 幂等、越界拒绝。
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "test_framework.h"
#include "logengine/file_apply.h"

using namespace logc;

namespace {

std::string tmpPath(const char *name) {
  // 落在本仓库 tests/ 旁的临时文件（测试后清理）；不用 /tmp 保持跨平台。
  return std::string("file_apply_test_") + name;
}

void writeRaw(const std::string &path, const std::string &data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f << data;
}

std::string readRaw(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
}

TextEdit mkEdit(int line, int col, const std::string &text,
                const std::string &file = "") {
  TextEdit e;
  e.start.line = line;
  e.start.col = col;
  e.end = e.start;
  e.text = text;
  e.file = file;
  return e;
}

void cleanup(const std::vector<std::string> &files) {
  for (auto &f : files) std::remove(f.c_str());
}

}  // namespace

TEST_CASE(file_apply_insert_end_of_line) {
  // col = 行长+1 → 插到行尾（换行符之前）
  std::string p = tmpPath("eol.cpp");
  writeRaw(p, "int a;\nint b;\n");
  TextEdit e = mkEdit(1, 8, "  // tail");
  auto r = applyEditsToFiles(p, {e});
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.filesWritten, 1);
  EXPECT_EQ_STR(readRaw(p), "int a;  // tail\nint b;\n");
  cleanup({p});
}

TEST_CASE(file_apply_insert_descending_order) {
  // 同文件两条插入：降序应用，行号不漂移
  std::string p = tmpPath("desc.cpp");
  writeRaw(p, "l1\nl2\nl3\n");
  auto r = applyEditsToFiles(p, {mkEdit(1, 3, "A"), mkEdit(3, 1, "B")});
  EXPECT_TRUE(r.ok);
  EXPECT_EQ_STR(readRaw(p), "l1A\nl2\nBl3\n");
  cleanup({p});
}

TEST_CASE(file_apply_crlf_preserved) {
  // CRLF 文件：行尾风格保持，注入文本的 \n 也转成 CRLF
  std::string p = tmpPath("crlf.cpp");
  writeRaw(p, "int a;\r\nint b;\r\n");
  auto r = applyEditsToFiles(
      p, {mkEdit(1, 8, "  // t\nint x;")});
  EXPECT_TRUE(r.ok);
  EXPECT_EQ_STR(readRaw(p), "int a;  // t\r\nint x;\r\nint b;\r\n");
  cleanup({p});
}

TEST_CASE(file_apply_lf_not_broken_on_crlf_source) {
  // LF 文件注入多行文本：保持 LF
  std::string p = tmpPath("lf.cpp");
  writeRaw(p, "int a;\n");
  auto r = applyEditsToFiles(p, {mkEdit(1, 1, "// x\n")});
  EXPECT_TRUE(r.ok);
  EXPECT_EQ_STR(readRaw(p), "// x\nint a;\n");
  cleanup({p});
}

TEST_CASE(file_apply_bom_preserved) {
  std::string p = tmpPath("bom.cpp");
  writeRaw(p, "\xEF\xBB\xBFint a;\n");
  auto r = applyEditsToFiles(p, {mkEdit(1, 1, "// x\n")});
  EXPECT_TRUE(r.ok);
  EXPECT_EQ_STR(readRaw(p), "\xEF\xBB\xBF// x\nint a;\n");
  cleanup({p});
}

TEST_CASE(file_apply_cross_file_group) {
  // e.file 非空 → 写那个文件；主文件也有一条
  std::string main = tmpPath("m.cpp");
  std::string hdr = tmpPath("w.h");
  writeRaw(main, "int main() { return 0; }\n");
  writeRaw(hdr, "struct W {};\n");
  auto r = applyEditsToFiles(
      main, {mkEdit(1, 1, "// m\n"), mkEdit(1, 15, "  // h\n", hdr)});
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.filesWritten, 2);
  EXPECT_EQ_STR(readRaw(main), "// m\nint main() { return 0; }\n");
  // "struct W {};" 长 11，col=15 → 钳到 12（行尾，原 \n 之前）。
  // 插入 "  // h\n"：行变 "struct W {};  // h" + 原 \n 换到新行尾 →
  // 与 VSCode 行尾 insert 相同：text 的 \n 结束当前行，原文 \n 开启空行。
  EXPECT_EQ_STR(readRaw(hdr), "struct W {};  // h\n\n");
  cleanup({main, hdr});
}

TEST_CASE(file_apply_no_edits_noop) {
  // 无 edit：不写文件（幂等重跑），且不要求文件存在
  std::string p = tmpPath("missing_noop.cpp");
  std::remove(p.c_str());
  auto r = applyEditsToFiles(p, {});
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.filesWritten, 0);
}

TEST_CASE(file_apply_missing_target_error) {
  // 有 edit 但目标文件不存在 → 报错
  std::string p = tmpPath("missing.cpp");
  std::remove(p.c_str());
  auto r = applyEditsToFiles(p, {mkEdit(1, 1, "x")});
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.error.empty());
}

TEST_CASE(file_apply_line_out_of_range_error) {
  std::string p = tmpPath("oor.cpp");
  writeRaw(p, "a\n");
  auto r = applyEditsToFiles(p, {mkEdit(5, 1, "x")});
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.error.empty());
  EXPECT_CONTAINS(r.error, "out of range");
  cleanup({p});
}

TEST_CASE(file_apply_col_clamped) {
  // col 超出行长 → 钳到行尾，不报错（与插件 byteColToUtf16 的宽容一致）
  std::string p = tmpPath("clamp.cpp");
  writeRaw(p, "ab\n");
  auto r = applyEditsToFiles(p, {mkEdit(1, 99, "X")});
  EXPECT_TRUE(r.ok);
  EXPECT_EQ_STR(readRaw(p), "abX\n");
  cleanup({p});
}

TEST_CASE(file_apply_utf8_multibyte_not_split) {
  // 列落在多字节字符中间 → 回退到字符起始字节，不劈开 UTF-8 序列
  // 行 = "你" (3 字节) + "x"。col=2/3/4 都指进 "你" 内部 → 回退 col=1
  std::string p = tmpPath("utf8.cpp");
  writeRaw(p, "\xE4\xBD\xA0x\n");
  auto r = applyEditsToFiles(p, {mkEdit(1, 3, "X")});
  EXPECT_TRUE(r.ok);
  EXPECT_EQ_STR(readRaw(p), "X\xE4\xBD\xA0x\n");
  cleanup({p});
}

TEST_CASE(file_apply_last_line_no_trailing_newline) {
  // 文件不以换行结尾：最后一行仍可插入，写回后仍无换行
  std::string p = tmpPath("nonl.cpp");
  writeRaw(p, "int a;");
  auto r = applyEditsToFiles(p, {mkEdit(1, 8, "  // t")});
  EXPECT_TRUE(r.ok);
  EXPECT_EQ_STR(readRaw(p), "int a;  // t");
  cleanup({p});
}

TEST_CASE(file_apply_range_edit_rejected) {
  // 协议当前只有纯插入；区间替换应拒绝而非静默写错
  std::string p = tmpPath("range.cpp");
  writeRaw(p, "int a;\n");
  TextEdit e = mkEdit(1, 1, "X");
  e.end.line = 1;
  e.end.col = 4;
  auto r = applyEditsToFiles(p, {e});
  EXPECT_FALSE(r.ok);
  EXPECT_CONTAINS(r.error, "range-replace");
  cleanup({p});
}

TEST_CASE(file_apply_same_line_multi_edits) {
  // 同行多条插入：都按各自 col 生效（降序应用，col 大者先）。
  // col=3 前插 "2"：abcdef → ab2cdef；col=2 前插 "1"：a1bcdef。
  // 两处不重叠，最终 a1b2cdef。
  std::string p = tmpPath("sameline.cpp");
  writeRaw(p, "abcdef\n");
  auto r = applyEditsToFiles(p, {mkEdit(1, 3, "2"), mkEdit(1, 2, "1")});
  EXPECT_TRUE(r.ok);
  EXPECT_EQ_STR(readRaw(p), "a1b2cdef\n");
  cleanup({p});
}
