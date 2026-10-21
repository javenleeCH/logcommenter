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
  // MSVC(CMake) compile_commands：command 自带源文件路径 + /Fo 输出参数。
  // 用户传正斜杠绝对路径 --file，command 里是反斜杠绝对路径 → 应剥离重复源文件 token，
  // 保留 /Fo 与其余编译选项。
  std::vector<std::string> in = {
      "cl.exe",
      "/nologo",
      "/c",
      "/FoCMakeFiles\\sample.dir\\src\\sample.cpp.obj",
      "/Iinc",
      "D:\\codeSpace\\CDir\\vkdemo\\src\\main.cpp"};
  auto out = stripSourceFileArg(in, "D:/codeSpace/CDir/vkdemo/src/main.cpp");
  // 源文件 token 已剥离：任何参数不应再以 main.cpp 结尾
  for (auto &s : out)
    EXPECT_FALSE(s.size() >= 8 && s.compare(s.size() - 8, 8, "main.cpp") == 0);
  bool hasFo = false, hasInc = false;
  for (auto &s : out) {
    if (s.rfind("/Fo", 0) == 0) hasFo = true;
    if (s == "/Iinc") hasInc = true;
  }
  EXPECT_TRUE(hasFo);
  EXPECT_TRUE(hasInc);
  // 剩 cl.exe /nologo /c /Fo... /Iinc = 5 项
  EXPECT_EQ(static_cast<int>(out.size()), 5);
  // 相对路径 --file（.\\src\\main.cpp）也应按文件名剥离
  auto out2 = stripSourceFileArg(in, ".\\src\\main.cpp");
  EXPECT_EQ(static_cast<int>(out2.size()), 5);
  // 无重复源文件的纯编译选项应原样保留
  auto out3 = stripSourceFileArg({"g++", "-c", "-Iinc"}, "x.cpp");
  EXPECT_EQ(static_cast<int>(out3.size()), 3);
}
