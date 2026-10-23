#pragma once
// TextEdit 结构与 JSON 序列化，供 CLI 输出与 applyEdit 使用。
#include <string>
#include <vector>

namespace logc {

// 1-based 行列（与 VSCode 一致）
struct Position {
  int line = 1;
  int col = 1;
};

struct TextEdit {
// 语义：start == end 为纯插入——text 插入到 (line,col) 处字符**之前**；
// col ∈ [1, 行长+1]，col = 行长+1 表示行尾（换行符之前）。start < end 为
// 区间替换（当前引擎未用到，保留给插件协议）。
  Position start;
  Position end;
  std::string text;
  // 目标文件（路径表示）。空 = 主文件（--file）；非空表示跨文件编辑
  // （如 friend 注入到类所在头文件）。见 editsToJson 的分组输出。
  std::string file;
};

// 将 file + edits 序列化为 JSON。所有 edit.file 为空时输出单文件对象
//   { "file": ..., "edits": [...] }（向后兼容既有插件 / E2E）；
// 存在跨文件 edit 时输出数组（按 file 分组，空 file 归入主 file）：
//   [ { "file": "a.cpp", "edits": [...] }, { "file": "a.h", "edits": [...] } ]
// 手写 JSON，无第三方依赖；text 内的 " 与 \ 会转义。
std::string editsToJson(const std::string &file,
                        const std::vector<TextEdit> &edits);

// 单条 JSON 字符串值转义
std::string jsonEscape(const std::string &s);

}  // namespace logc
