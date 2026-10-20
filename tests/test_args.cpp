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
