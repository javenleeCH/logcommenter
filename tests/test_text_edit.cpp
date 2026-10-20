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
