// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_granit_value.cpp
//
// Tests for the C#-flavored Value type (ObjectHandle, ManagedPointer, etc.).

#include <gtest/gtest.h>
#include "jade/tier0_granit/Value.hpp"

using namespace jade;
using namespace jade::granit;

TEST(GranitValueTest, MakeInt32StoresValue) {
    Value v = make_int32(42);
    ASSERT_TRUE(v.is_int32());
    EXPECT_EQ(v.as_int32(), 42);
}

TEST(GranitValueTest, MakeInt64StoresValue) {
    Value v = make_int64(0x123456789ABCDEF0LL);
    ASSERT_TRUE(v.is_int64());
    EXPECT_EQ(v.as_int64(), 0x123456789ABCDEF0LL);
}

TEST(GranitValueTest, MakeFloatStoresValue) {
    Value v = make_float(3.14);
    ASSERT_TRUE(v.is_float());
    EXPECT_DOUBLE_EQ(v.as_float(), 3.14);
}

TEST(GranitValueTest, MakeNullObjectStoresNullHandle) {
    Value v = make_null_object();
    ASSERT_TRUE(v.is_object());
    EXPECT_TRUE(v.as_object().is_null());
}

TEST(GranitValueTest, ObjectHandleIndexAndGenerationSplitCorrectly) {
    ObjectHandle h{0x0000'0002'0000'0007ULL};
    EXPECT_EQ(h.index(), 7u);
    EXPECT_EQ(h.generation(), 2u);
}

TEST(GranitValueTest, ObjectHandleNullIsValueZero) {
    ObjectHandle h{};
    EXPECT_EQ(h.value, 0ULL);
    EXPECT_TRUE(h.is_null());
    EXPECT_FALSE(h.valid());
}

TEST(GranitValueTest, ManagedPointerStoresBaseAndOffset) {
    ObjectHandle h{0x1234};
    ManagedPointer p{h, 16};
    EXPECT_EQ(p.packed, (static_cast<uint64_t>(0x1234) << 32) | 16u);
}

TEST(GranitValueTest, EvalTypeOfMatchesVariantIndex) {
    EXPECT_EQ(eval_type_of(make_int32(0)),   EvalStackType::Int32);
    EXPECT_EQ(eval_type_of(make_int64(0)),   EvalStackType::Int64);
    EXPECT_EQ(eval_type_of(make_float(0.0)), EvalStackType::Float);
    EXPECT_EQ(eval_type_of(make_null_object()), EvalStackType::ObjectRef);
    EXPECT_EQ(eval_type_of(make_managed_ptr({})), EvalStackType::ManagedPtr);
    EXPECT_EQ(eval_type_of(make_native_ptr({})),   EvalStackType::NativePtr);
}

TEST(GranitValueTest, TruthyInt32FollowsCSharpSemantics) {
    EXPECT_TRUE(truthy(make_int32(1)));
    EXPECT_FALSE(truthy(make_int32(0)));
    EXPECT_TRUE(truthy(make_int32(-1)));
}

TEST(GranitValueTest, TruthyInt64FollowsCSharpSemantics) {
    EXPECT_TRUE(truthy(make_int64(1)));
    EXPECT_FALSE(truthy(make_int64(0)));
}

TEST(GranitValueTest, TruthyFloatFollowsCSharpSemantics) {
    EXPECT_TRUE(truthy(make_float(1.0)));
    EXPECT_FALSE(truthy(make_float(0.0)));
    EXPECT_TRUE(truthy(make_float(-1.5)));
}

TEST(GranitValueTest, TruthyNullObjectIsFalse) {
    EXPECT_FALSE(truthy(make_null_object()));
    ObjectHandle h{1};
    EXPECT_TRUE(truthy(make_object(h)));
}

TEST(GranitValueTest, ValueEqualsRequiresSameType) {
    EXPECT_TRUE(value_equals(make_int32(5), make_int32(5)));
    EXPECT_FALSE(value_equals(make_int32(5), make_int32(6)));
    EXPECT_FALSE(value_equals(make_int32(5), make_int64(5)));  // different types
}

TEST(GranitValueTest, ValueEqualsObjectHandles) {
    ObjectHandle h1{1};
    ObjectHandle h2{1};
    ObjectHandle h3{2};
    EXPECT_TRUE(value_equals(make_object(h1), make_object(h2)));
    EXPECT_FALSE(value_equals(make_object(h1), make_object(h3)));
}

TEST(GranitValueTest, ToStringFormatsAllTypes) {
    EXPECT_NE(to_string(make_int32(42)).find("int32:42"), std::string::npos);
    EXPECT_NE(to_string(make_int64(99)).find("int64:99"), std::string::npos);
    EXPECT_NE(to_string(make_float(3.14)).find("float"), std::string::npos);
    EXPECT_EQ(to_string(make_null_object()), "null");
}

TEST(GranitValueTest, WrapAddI32MatchesTwoComplement) {
    EXPECT_EQ(wrap_add_i32(1, 2), 3);
    EXPECT_EQ(wrap_add_i32(INT32_MAX, 1), INT32_MIN);  // overflow wraps
}

TEST(GranitValueTest, WrapMulI32MatchesTwoComplement) {
    EXPECT_EQ(wrap_mul_i32(3, 4), 12);
    EXPECT_EQ(wrap_mul_i32(INT32_MAX, 2), -2);  // overflow wraps
}

TEST(GranitValueTest, CheckedAddI32DetectsOverflow) {
    int32_t out;
    EXPECT_TRUE(checked_add_i32(1, 2, out));
    EXPECT_EQ(out, 3);
    EXPECT_FALSE(checked_add_i32(INT32_MAX, 1, out));
    EXPECT_FALSE(checked_add_i32(INT32_MIN, -1, out));
}

TEST(GranitValueTest, CheckedMulI32DetectsOverflow) {
    int32_t out;
    EXPECT_TRUE(checked_mul_i32(3, 4, out));
    EXPECT_EQ(out, 12);
    EXPECT_FALSE(checked_mul_i32(INT32_MAX, 2, out));
}
