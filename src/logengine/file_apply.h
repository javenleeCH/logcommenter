#pragma once
// --write：把 TextEdit 协议直接落到磁盘（CLI 侧写文件能力）。
// 语义与协议层（text_edit.h / vscode/src/protocol.ts）完全一致：
//   - 纯插入 start==end：text 插到 (line,col) 之前；
//   - col 是 1-based UTF-8 字节列，col = 行字节长+1 表示行尾；
//   - 多条 edit 同文件内按位置降序应用，行号互不影响；
//   - 保持原文件行尾风格（CRLF/LF）与 UTF-8 BOM；
//   - e.file 非空时写该文件（friend 跨文件注入），空写主文件。
// 纯逻辑（文件 IO 但零 Clang 依赖），可独立单测。
#include <string>
#include <vector>

#include "text_edit.h"

namespace logc {

struct WriteResult {
  bool ok = false;
  // 实际被写入的文件数（未变化的文件不计数）。失败时给 0。
  int filesWritten = 0;
  // 失败时的错误描述（文件路径 + 原因），成功为空。
  std::string error;
};

// 把 edits 应用到主文件 file（以及 e.file 非空的跨文件目标）并写盘。
// 主文件路径必须存在可读；跨文件目标不存在时报错（不凭空创建头文件）。
WriteResult applyEditsToFiles(const std::string &file,
                              const std::vector<TextEdit> &edits);

}  // namespace logc
