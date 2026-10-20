#include <iostream>
int main() {
  int total = 42;                                 // 供 --inject 观测的局部变量
  std::cout << "log: pengtest running" << std::endl;  // 注释目标（含 pengtest）
  return total;
}
