#pragma once
#include <string>
// 源文件"指令层 preamble"提取：PCH 缓存的原料。纯逻辑零依赖，可独立单测。
//
// 提取 sourceText 的"指令层 preamble"：保留所有空行、注释与预处理指令行，
// 代码行置空占位（行号与原文件一一对应）。PCH 以它为原料——指令层决定
// 预处理状态（代码行不影响宏/包含状态），故按此构建的 PCH 与全量解析等价：
// 主文件重放时宏重定义为同一值、头文件经 include guard 跳过，声明全部来自
// PCH 且位置正确。相比"纯文件前缀"模型（遇到第一行代码即截断），它能覆盖
// 文件中后部的 #include（如 vkdemo 第 15 行宏存储声明之后的 glm/glfw/std）。
// 局限（罕见，接受）：行尾反斜杠续行跨代码/指令边界、代码行内字符串包含
// "/*" 等构造可能偏离等价性；PCH 装载失败时引擎自动弃缓存裸解析兜底。

namespace logc {

std::string extractPreamble(const std::string &sourceText);

}  // namespace logc
