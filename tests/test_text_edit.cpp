#include "test_framework.h"
#include "logengine/text_edit.h"

using namespace logc;

TEST_CASE(json_escapes_quotes) {
  EXPECT_EQ_STR(jsonEscape("a\"b"), "a\\\"b");
  EXPECT_EQ_STR(jsonEscape("a\\b"), "a\\\\b");
  EXPECT_EQ_STR(jsonEscape("a\nb"), "a\\nb");
}

TEST_CASE(edits_to_json_shape) {
  TextEdit e;
  e.start = {42, 0};
  e.end = {42, 0};
  e.text = "std::cout << \"[obs] x=\" << x << std::endl;\n";
  std::string j = editsToJson("src/foo.cpp", {e});
  EXPECT_CONTAINS(j, "\"file\": \"src/foo.cpp\"");
  EXPECT_CONTAINS(j, "\"line\": 42");
  EXPECT_CONTAINS(j, "\"text\": \"");
  // 换行与引号都应被 JSON 转义
  EXPECT_CONTAINS(j, "\\n");
  EXPECT_CONTAINS(j, "\\\"");
}

TEST_CASE(edits_empty_array) {
  std::string j = editsToJson("a.cpp", {});
  EXPECT_CONTAINS(j, "\"edits\": [");
  EXPECT_CONTAINS(j, "]");
}

TEST_CASE(edits_to_json_single_file_legacy_shape) {
  // 无跨文件 edit：输出原单对象格式（向后兼容，E2E/插件不受影响）
  TextEdit e;
  e.start = {10, 5};
  e.end = {10, 5};
  e.text = "x();";
  std::string j = editsToJson("a.cpp", {e});
  EXPECT_CONTAINS(j, "{\n  \"file\": \"a.cpp\"");
  EXPECT_FALSE(j.rfind("[", 0) == 0);  // 不是数组
}

TEST_CASE(edits_to_json_multi_file) {
  // 跨文件（friend 注入）：输出按文件分组的数组，空 file 归入主文件
  TextEdit main;
  main.start = {11, 1};
  main.end = {11, 1};
  main.text = "obs;";
  TextEdit hdr;
  hdr.start = {5, 3};
  hdr.end = {5, 3};
  hdr.text = "friend ...;";
  hdr.file = "widget.h";
  std::string j = editsToJson("a.cpp", {main, hdr});
  // 数组开头 + 两个文件组 + 顺序保持
  EXPECT_TRUE(j.rfind("[", 0) == 0);
  EXPECT_CONTAINS(j, "\"file\": \"a.cpp\"");
  EXPECT_CONTAINS(j, "\"file\": \"widget.h\"");
  EXPECT_TRUE(j.find("\"file\": \"a.cpp\"") < j.find("\"file\": \"widget.h\""));
  EXPECT_CONTAINS(j, "friend ...;");
}

TEST_CASE(edits_to_json_multi_file_same_group_merged) {
  // 同一跨文件目标的多个 edit 应合并进同一组（组内按序两条）
  TextEdit a;
  a.start = {1, 1};
  a.end = {1, 1};
  a.text = "x;";
  a.file = "w.h";
  TextEdit b;
  b.start = {2, 1};
  b.end = {2, 1};
  b.text = "y;";
  b.file = "w.h";
  std::string j = editsToJson("a.cpp", {a, b});
  EXPECT_TRUE(j.rfind("[", 0) == 0);
  EXPECT_CONTAINS(j, "\"file\": \"w.h\"");
  // w.h 组只出现一次（合并）
  size_t first = j.find("\"file\": \"w.h\"");
  EXPECT_TRUE(j.find("\"file\": \"w.h\"", first + 1) == std::string::npos);
  EXPECT_CONTAINS(j, "x;");
  EXPECT_CONTAINS(j, "y;");
}

TEST_CASE(edits_to_json_multi_file_empty) {
  // 空 edits：无跨文件 edit 时保持单对象格式
  std::string j = editsToJson("a.cpp", {});
  EXPECT_FALSE(j.rfind("[", 0) == 0);
  EXPECT_CONTAINS(j, "\"file\": \"a.cpp\"");
  EXPECT_CONTAINS(j, "\"edits\": [");
}
