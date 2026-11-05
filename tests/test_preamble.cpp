#include <string>

#include "test_framework.h"
#include "logengine/preamble.h"

using namespace logc;

TEST_CASE(preamble_directives_and_code_blanked) {
  // 指令层模型：代码行置空占位（行号一致），中后部 include 保留
  std::string src =
      "#define A 1\n"          // 1 指令
      "#include <vector>\n"     // 2 指令
      "int x = 42;\n"           // 3 代码 → 空
      "VULKAN_STORAGE_MACRO\n"  // 4 代码 → 空
      "#include <string>\n"     // 5 指令（前缀模型会丢，指令层模型保留）
      "int main() { return x; }\n";
  auto p = extractPreamble(src);
  EXPECT_EQ_STR(p, "#define A 1\n#include <vector>\n\n\n#include <string>\n\n");
}

TEST_CASE(preamble_no_include_returns_empty) {
  EXPECT_EQ_STR(extractPreamble("int main(){return 0;}\n"), "");
  EXPECT_EQ_STR(extractPreamble("#define A 1\n#define B 2\n"), "");
  EXPECT_EQ_STR(extractPreamble(""), "");
}

TEST_CASE(preamble_comments_and_blank_kept) {
  std::string src =
      "// line comment\n"
      "\n"
      "/* block start\n"
      "   block body\n"
      "*/\n"
      "#include <a>\n";
  auto p = extractPreamble(src);
  EXPECT_EQ_STR(p, src);
}

TEST_CASE(preamble_code_line_opening_block_comment) {
  // 代码行内开启跨行注释：代码置空但补 "/*" 维持注释结构
  std::string src = "#include <a>\nint x; /* open\nstill comment\n*/\n";
  auto p = extractPreamble(src);
  EXPECT_EQ_STR(p, "#include <a>\n/* open\nstill comment\n*/\n");
}

TEST_CASE(preamble_comment_close_line_trims_code) {
  // 行内闭合注释：只保留到 "*/"，其后代码置空
  std::string src = "#include <a>\n/* c */ int y = 2;\n";
  auto p = extractPreamble(src);
  EXPECT_EQ_STR(p, "#include <a>\n/* c */\n");
}

TEST_CASE(preamble_bom_preserved_and_crlf_normalized) {
  std::string src = "\xEF\xBB\xBF#include <a>\r\nint x;\r\n";
  auto p = extractPreamble(src);
  EXPECT_EQ_STR(p, "\xEF\xBB\xBF#include <a>\n\n");
}

TEST_CASE(preamble_string_literal_with_comment_marker) {
  // 代码行字符串中的 /* 不影响注释状态
  std::string src = "#include <a>\nconst char* s = \"/*\";\n#include <b>\n";
  auto p = extractPreamble(src);
  EXPECT_EQ_STR(p, "#include <a>\n\n#include <b>\n");
}

TEST_CASE(preamble_vkdemo_shape) {
  // vkdemo main.cpp 形态：第 15 行宏存储声明截断纯前缀，指令层仍覆盖后续 include
  std::string src =
      "#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1\n"
      "#include <vulkan/vulkan.hpp>\n"
      "#include <GLFW/glfw3.h>\n"
      "#define GLM_FORCE_RADIANS\n"
      "#include <glm/glm.hpp>\n"
      "VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE\n"
      "#include <iostream>\n"
      "#include <vector>\n"
      "const uint32_t WIDTH = 800;\n";
  auto p = extractPreamble(src);
  EXPECT_EQ_STR(p, "#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1\n"
                   "#include <vulkan/vulkan.hpp>\n"
                   "#include <GLFW/glfw3.h>\n"
                   "#define GLM_FORCE_RADIANS\n"
                   "#include <glm/glm.hpp>\n"
                   "\n"
                   "#include <iostream>\n"
                   "#include <vector>\n"
                   "\n");
}
