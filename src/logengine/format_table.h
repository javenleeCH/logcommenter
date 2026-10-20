#pragma once
// 观测格式表：依据 QualType 字符串生成打印表达式。
// 不依赖 Clang，供 AST 适配层与单元测试复用。
#include <string>

namespace logc {

// 观测格式：能否直接 << 打印，以及要打印的表达式
struct ObsFormat {
  bool streamable = false;  // 能否直接用于 std::cout << ...
  std::string expr;         // streamable==true 时，要 << 的表达式
};

// 纯字符串启发式映射：
//   int/float/double/char*/std::string/指针/bool 等 → 可直接打印
//   复杂对象（含 ::、模板、类名等）→ 不可直接打印，交由上层走 operator<</friend/地址兜底
ObsFormat formatForType(const std::string &qualType, const std::string &var);

// 规整 QualType 字符串：去掉 const/volatile/引用/前导空格，返回核心类型片段
std::string simplifyType(const std::string &qualType);

// 判定一个 QualType 字符串是否属于"可直接流式打印"的基础类型
bool isPrimitiveStreamable(const std::string &simplified);

}  // namespace logc
