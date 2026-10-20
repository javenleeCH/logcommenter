#include "test_framework.h"
#include "logengine/format_table.h"

using namespace logc;

TEST_CASE(format_primitive_int) {
  auto f = formatForType("int", "x");
  EXPECT_TRUE(f.streamable);
  EXPECT_EQ_STR(f.expr, "x");
}

TEST_CASE(format_primitive_double) {
  auto f = formatForType("const double", "d");
  EXPECT_TRUE(f.streamable);
  EXPECT_EQ_STR(f.expr, "d");
}

TEST_CASE(format_primitive_string) {
  auto f = formatForType("std::string", "s");
  EXPECT_TRUE(f.streamable);
  EXPECT_EQ_STR(f.expr, "s");
}

TEST_CASE(format_primitive_charptr) {
  auto f = formatForType("const char *", "msg");
  EXPECT_TRUE(f.streamable);
}

TEST_CASE(format_primitive_bool_readable) {
  auto f = formatForType("bool", "flag");
  EXPECT_TRUE(f.streamable);
  EXPECT_CONTAINS(f.expr, "\"true\"");
}

TEST_CASE(format_pointer_prints_address) {
  auto f = formatForType("MyClass *", "obj");
  EXPECT_TRUE(f.streamable);
  EXPECT_EQ_STR(f.expr, "obj");
}

TEST_CASE(format_object_not_streamable) {
  // 无 operator<< 的类对象：不可直接流式打印，交由上层处理
  auto f = formatForType("MyClass", "obj");
  EXPECT_FALSE(f.streamable);
}

TEST_CASE(format_template_object_not_streamable) {
  auto f = formatForType("std::vector<int>", "v");
  EXPECT_FALSE(f.streamable);
}

TEST_CASE(simplify_strips_cvref) {
  EXPECT_EQ_STR(simplifyType("const int"), "int");
  EXPECT_EQ_STR(simplifyType("const volatile double &"), "double");
  EXPECT_EQ_STR(simplifyType("struct Foo"), "Foo");
}
