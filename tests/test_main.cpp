// 测试入口：运行所有 TEST_CASE 注册的用例。
#include <cstdio>
#include <string>

#include "test_framework.h"

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  auto &cases = testfw::registry();
  int run = 0, fail_cases = 0;
  for (auto &c : cases) {
    ++run;
    int before = testfw::failures();
    std::printf("[ RUN ] %s\n", c.name.c_str());
    c.fn();
    int added = testfw::failures() - before;
    if (added) {
      ++fail_cases;
      std::printf("[FAIL ] %s (%d assertions)\n", c.name.c_str(), added);
    } else {
      std::printf("[  OK ] %s\n", c.name.c_str());
    }
  }
  std::printf("==== %d cases, %d checks, %d failures ====\n", run,
              testfw::checks(), testfw::failures());
  return fail_cases ? 1 : 0;
}
