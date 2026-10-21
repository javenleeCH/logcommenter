#include <iostream>
#include <string>

// 对象类型：用于验证对象 fallback（不可直接 <<，打印地址）
struct Config {
  int level = 3;
};

int main() {
  int total = 42;                            // 供 --inject 观测的局部变量（基本类型）
  Config cfg;                                // 对象类型：注入走 (ptr) 地址兜底
  std::string name = "demo";                 // 标准库对象：注入走 (ptr) 地址兜底
  std::cout << "log: pengtest running" << std::endl;  // 注释目标（含 pengtest）
  return total;
}
