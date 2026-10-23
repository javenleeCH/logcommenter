#pragma once
// B 组系统测试专用头：验证三种对象观测路径
//   1) Widget    —— 头文件里有 operator<<（inline 自由函数）→ operator<< 实查
//   2) Gadget    —— 无 operator<< → --friend 注入 或 (ptr) 地址兜底
//   3) Box<T>    —— 模板类 → --friend 应跳过（isTemplated），地址兜底
#include <ostream>

struct Widget {
  int id;
  explicit Widget(int i) : id(i) {}
};

inline std::ostream &operator<<(std::ostream &os, const Widget &w) {
  return os << "Widget{" << w.id << "}";
}

struct Gadget {
  int level = 1;
};

template <typename T>
struct Box {
  T v{};
};
