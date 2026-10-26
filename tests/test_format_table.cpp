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

// ---- 固定宽度整数 (<cstdint>) ----

TEST_CASE(format_uint32_t_streamable) {
  auto f = formatForType("uint32_t", "imageIndex");
  EXPECT_TRUE(f.streamable);
  EXPECT_EQ_STR(f.expr, "imageIndex");
}

TEST_CASE(format_uint64_t_streamable) {
  auto f = formatForType("uint64_t", "timestamp");
  EXPECT_TRUE(f.streamable);
  EXPECT_EQ_STR(f.expr, "timestamp");
}

TEST_CASE(format_int32_t_streamable) {
  auto f = formatForType("int32_t", "offset");
  EXPECT_TRUE(f.streamable);
  EXPECT_EQ_STR(f.expr, "offset");
}

TEST_CASE(format_int64_t_streamable) {
  auto f = formatForType("int64_t", "size");
  EXPECT_TRUE(f.streamable);
  EXPECT_EQ_STR(f.expr, "size");
}

TEST_CASE(format_uint8_t_streamable) {
  auto f = formatForType("uint8_t", "byte");
  EXPECT_TRUE(f.streamable);
}

TEST_CASE(format_uint16_t_streamable) {
  auto f = formatForType("uint16_t", "port");
  EXPECT_TRUE(f.streamable);
}

TEST_CASE(format_int8_t_streamable) {
  auto f = formatForType("int8_t", "level");
  EXPECT_TRUE(f.streamable);
}

TEST_CASE(format_int16_t_streamable) {
  auto f = formatForType("int16_t", "delta");
  EXPECT_TRUE(f.streamable);
}

TEST_CASE(format_const_uint32_t_ref_streamable) {
  auto f = formatForType("const uint32_t &", "idx");
  EXPECT_TRUE(f.streamable);
  EXPECT_EQ_STR(f.expr, "idx");
}

TEST_CASE(format_uintptr_t_streamable) {
  auto f = formatForType("uintptr_t", "addr");
  EXPECT_TRUE(f.streamable);
}

TEST_CASE(format_intptr_t_streamable) {
  auto f = formatForType("intptr_t", "paddr");
  EXPECT_TRUE(f.streamable);
}

TEST_CASE(format_intmax_t_streamable) {
  auto f = formatForType("intmax_t", "bigval");
  EXPECT_TRUE(f.streamable);
}

TEST_CASE(format_uintmax_t_streamable) {
  auto f = formatForType("uintmax_t", "ubigval");
  EXPECT_TRUE(f.streamable);
}

TEST_CASE(format_char16_t_streamable) {
  auto f = formatForType("char16_t", "uc");
  EXPECT_TRUE(f.streamable);
}

TEST_CASE(format_char32_t_streamable) {
  auto f = formatForType("char32_t", "u32c");
  EXPECT_TRUE(f.streamable);
}

TEST_CASE(format_ptrdiff_t_streamable) {
  auto f = formatForType("ptrdiff_t", "diff");
  EXPECT_TRUE(f.streamable);
}

TEST_CASE(simplify_strips_cvref) {
  EXPECT_EQ_STR(simplifyType("const int"), "int");
  EXPECT_EQ_STR(simplifyType("const volatile double &"), "double");
  EXPECT_EQ_STR(simplifyType("struct Foo"), "Foo");
}
