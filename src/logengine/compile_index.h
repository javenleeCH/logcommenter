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
  // 流式扫描 jsonPath，建立 file -> (offset,length) 索引。offset 指向条目 '{'，
  // length 覆盖该条目 JSON（含闭合 '}'）。失败返回 false 并写 err。
  bool build(const std::string &jsonPath, std::string *err = nullptr);

  // 查询某文件条目的字节区间；未命中返回 false
  bool locate(const std::string &file, size_t *offset,
              size_t *length) const;

  // 读取该文件的原始条目 JSON 文本（seek+read，不加载全量文件）
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
  std::string jsonPath_;
  std::map<std::string, std::pair<size_t, size_t>> idx_;
  std::time_t mtime_ = 0;
};

}  // namespace logc
