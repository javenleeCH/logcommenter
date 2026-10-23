#pragma once
// 偏移索引：将 compile_commands.json 中每个 file 条目映射到 (offset, length)。
// 构建 = 一次性流式扫描；查询 = seek+read 原文件那一条，O(1)，不加载全量。
#include <cstdint>
#include <ctime>
#include <map>
#include <string>

namespace logc {

class CompileIndex {
 public:
  // 单条目索引：file 的字节区间 + directory 字段内容区间（未反转义，按需读取）。
  // dirLen == 0 表示该条目没有提取到 directory，相对路径兜底将退化。
  struct Entry {
    size_t offset = 0;
    size_t length = 0;
    size_t dirOff = 0;
    size_t dirLen = 0;
  };

  // 流式扫描 jsonPath，建立 file -> Entry 索引。offset 指向条目 '{'，
  // length 覆盖该条目 JSON（含闭合 '}'）。失败返回 false 并写 err。
  bool build(const std::string &jsonPath, std::string *err = nullptr);

  // 查询某文件条目的字节区间；未命中返回 false
  bool locate(const std::string &file, size_t *offset,
              size_t *length) const;

  // 读取该文件的原始条目 JSON 文本（seek+read，不加载全量文件）。
  // file 可为相对路径：未精确命中时按"绝对路径 + 条目 directory 拼接"兜底匹配。
  bool readEntry(const std::string &file, std::string *entryJson) const;

  // 持久化索引到 cachePath（JSON lines），供下次直接加载，避免重扫 1GB
  bool saveCache(const std::string &cachePath) const;

  // 从 cachePath 加载索引；jsonPath 为 compile_commands.json 路径（用于 mtime 失效判断）
  bool loadCache(const std::string &cachePath, const std::string &jsonPath);

  size_t size() const { return idx_.size(); }
  const std::string &path() const { return jsonPath_; }

  // 索引是否过期（compile_commands.json 的 mtime 变化）
  bool isStale() const;

  // 当前 JSON 文件的 mtime（已过期或不存在返回 0）
  std::time_t sourceMtime() const;

 private:
  // 按已知 (offset,length) seek+read 单条条目
  bool readAt(size_t offset, size_t length, std::string *entryJson) const;
  std::string jsonPath_;
  std::map<std::string, Entry> idx_;
  std::time_t mtime_ = 0;
};

}  // namespace logc
