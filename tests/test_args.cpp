#include <cstdio>
#include <cstdlib>
#include <string>

#include "test_framework.h"
#include "logengine/args.h"

using namespace logc;

namespace {
CliArgs parseVec(std::vector<const char *> v) {
  return parseArgs(static_cast<int>(v.size()),
                   const_cast<const char **>(v.data()));
}
}  // namespace

TEST_CASE(args_inject_full) {
  std::vector<const char *> v = {"prog", "--inject", "--file", "a.cpp",
                                 "--var", "x", "--position", "42:17",
                                 "--compile-commands", "cc.json"};
  auto a = parseVec(v);
  EXPECT_TRUE(a.error.empty());
  EXPECT_TRUE(a.action == Action::kInject);
  EXPECT_EQ_STR(a.file, "a.cpp");
  EXPECT_EQ_STR(a.var, "x");
  EXPECT_EQ(a.line, 42);
  EXPECT_EQ(a.col, 17);
  EXPECT_EQ_STR(a.compileJson, "cc.json");
}

TEST_CASE(args_comment) {
  std::vector<const char *> v = {"prog", "--comment", "--file", "a.cpp",
                                 "--compile-commands", "cc.json"};
  auto a = parseVec(v);
  EXPECT_TRUE(a.action == Action::kComment);
  EXPECT_TRUE(a.error.empty());
}

TEST_CASE(args_unknown_option) {
  std::vector<const char *> v = {"prog", "--bogus"};
  auto a = parseVec(v);
  EXPECT_FALSE(a.error.empty());
}

TEST_CASE(args_bad_position) {
  std::vector<const char *> v = {"prog", "--inject", "--position", "abc:def"};
  auto a = parseVec(v);
  EXPECT_FALSE(a.error.empty());
}

TEST_CASE(parse_position_valid) {
  int l = 0, c = 0;
  EXPECT_TRUE(parsePosition("7:3", &l, &c));
  EXPECT_EQ(l, 7);
  EXPECT_EQ(c, 3);
}

TEST_CASE(parse_position_invalid) {
  int l = 0, c = 0;
  EXPECT_FALSE(parsePosition("x:y", &l, &c));
  EXPECT_FALSE(parsePosition("5", &l, &c));
  EXPECT_FALSE(parsePosition("0:1", &l, &c));
}

TEST_CASE(args_strip_duplicate_source) {
  // MSVC(CMake) compile_commands：command 自带源文件路径 + /Fo 输出参数，ClangTool 又会注入
  // 一次源文件（可能换斜杠/绝对形式）→ 出现重复源文件。应剥离**多余的副本、保留一个**，
  // 否则"no input files"（全剥光）或"cannot specify /Fo when compiling multiple source files"。
  std::vector<std::string> in = {
      "cl.exe",
      "/nologo",
      "/c",
      "/FoCMakeFiles\\sample.dir\\src\\main.cpp.obj",
      "/Iinc",
      "D:\\codeSpace\\CDir\\vkdemo\\src\\main.cpp",   // command 里的（反斜杠绝对）
      "D:/codeSpace/CDir/vkdemo/src/main.cpp"};        // ClangTool 注入的（正斜杠绝对）
  auto out = stripSourceFileArg(in, "D:/codeSpace/CDir/vkdemo/src/main.cpp");
  // 保留一个 main.cpp，剥离另一个副本 → 7 - 1 = 6 项
  EXPECT_EQ(static_cast<int>(out.size()), 6);
  bool hasFo = false, hasInc = false, keptSrc = false;
  for (auto &s : out) {
    if (s.rfind("/Fo", 0) == 0) hasFo = true;
    if (s == "/Iinc") hasInc = true;
    if (s.size() >= 8 && s.compare(s.size() - 8, 8, "main.cpp") == 0) keptSrc = true;
  }
  EXPECT_TRUE(hasFo);
  EXPECT_TRUE(hasInc);
  EXPECT_TRUE(keptSrc);  // 必须仍有一个源文件输入
  // 相对路径 --file（.\\src\\main.cpp）也按文件名剥离多余副本、保留一个
  auto out2 = stripSourceFileArg(in, ".\\src\\main.cpp");
  EXPECT_EQ(static_cast<int>(out2.size()), 6);
  // 单个源文件（无重复）应原样保留，不能被剥光
  auto out3 = stripSourceFileArg({"cl", "/c", "src\\main.cpp"}, "src/main.cpp");
  EXPECT_EQ(static_cast<int>(out3.size()), 3);
  // 无源文件的纯编译选项原样保留
  auto out4 = stripSourceFileArg({"g++", "-c", "-Iinc"}, "x.cpp");
  EXPECT_EQ(static_cast<int>(out4.size()), 3);
}
