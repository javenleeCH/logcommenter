#include <iostream>
#include <string>
#include "friend_widget.h"

// B 组系统测试专用源：覆盖 operator<< 实查 / --friend 注入 / 地址兜底。
// 布局行号与 tools/e2e_bgroup.py 无强耦合（脚本不依赖具体行号），
// 但修改本节变量名会破坏 e2e_bgroup.py 断言，需同步。

int main() {
  Widget w(7);          // 头文件有 operator<< → 实查流式 [obs] w=
  Gadget g;             // 无 operator<< → --friend 注入流式 / 否则 (ptr)
  Box<int> bx;          // 模板类 → --friend 跳过，始终 (ptr)
  std::string s = "bgroup";
  std::cout << "bgroup running" << std::endl;
  return w.id;
}
