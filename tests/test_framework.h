#pragma once
// 轻量测试框架：无第三方依赖，注册用例 + 断言，main 由 test_main.cpp 提供。
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace testfw {

struct Case {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<Case> &registry() {
  static std::vector<Case> r;
  return r;
}

struct Reg {
  Reg(const std::string &n, std::function<void()> f) {
    registry().push_back({n, std::move(f)});
  }
};

inline int &failures() {
  static int f = 0;
  return f;
}

inline int &checks() {
  static int c = 0;
  return c;
}

inline void report(const char *file, int line, const char *msg) {
  std::printf("  FAIL %s:%d: %s\n", file, line, msg);
  ++failures();
}

}  // namespace testfw

#define TEST_CASE(name)                                           \
  static void test_##name();                                      \
  static testfw::Reg reg_##name(#name, test_##name);              \
  static void test_##name()

#define EXPECT_TRUE(cond)                                          \
  do {                                                             \
    ++testfw::checks();                                            \
    if (!(cond)) testfw::report(__FILE__, __LINE__, "EXPECT_TRUE(" #cond ")"); \
  } while (0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#define EXPECT_EQ(a, b)                                            \
  do {                                                             \
    ++testfw::checks();                                            \
    auto va = (a);                                                 \
    auto vb = (b);                                                 \
    if (!(va == vb))                                               \
      testfw::report(__FILE__, __LINE__, "EXPECT_EQ(" #a ", " #b ")"); \
  } while (0)

#define EXPECT_EQ_STR(a, b) EXPECT_EQ(std::string(a), std::string(b))

#define EXPECT_CONTAINS(hay, needle)                               \
  do {                                                             \
    ++testfw::checks();                                            \
    std::string _h = (hay), _n = (needle);                         \
    if (_h.find(_n) == std::string::npos)                          \
      testfw::report(__FILE__, __LINE__,                           \
                     "EXPECT_CONTAINS(" #hay ", " #needle ")");    \
  } while (0)
