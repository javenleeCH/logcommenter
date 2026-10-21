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
};

// 将 file + edits 序列化为 { "file": ..., "edits": [ {range,text} ] }
// 手写 JSON，无第三方依赖；text 内的 " 与 \ 会转义。
std::string editsToJson(const std::string &file,
                        const std::vector<TextEdit> &edits);

// 单条 JSON 字符串值转义
std::string jsonEscape(const std::string &s);

}  // namespace logc
