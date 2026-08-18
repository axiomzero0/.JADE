// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_core_flags.cpp

#include <gtest/gtest.h>
#include "jade/core/Flags.hpp"
#include "jade/ir/NodeFlag.hpp"

using namespace jade;

namespace {

enum class TestFlag : uint16_t {
    None  = 0,
    A     = 1u << 0,
    B     = 1u << 1,
    C     = 1u << 2,
    D     = 1u << 3,
};
using TestFlags = Flags<TestFlag>;

}  // namespace

TEST(FlagsTest, DefaultIsEmpty) {
    TestFlags f;
    EXPECT_TRUE(f.empty());
}

TEST(FlagsTest, SingleBitSetHasThatBit) {
    TestFlags f = TestFlag::A;
    EXPECT_TRUE(f.has(TestFlag::A));
    EXPECT_FALSE(f.has(TestFlag::B));
}

TEST(FlagsTest, OrCombinesBits) {
    TestFlags f = TestFlag::A | TestFlag::C;
    EXPECT_TRUE(f.has(TestFlag::A));
    EXPECT_TRUE(f.has(TestFlag::C));
    EXPECT_FALSE(f.has(TestFlag::B));
}

TEST(FlagsTest, HasAllOfRequiresAll) {
    TestFlags f = TestFlag::A | TestFlag::B | TestFlag::C;
    EXPECT_TRUE(f.has_all_of(TestFlag::A | TestFlag::B));
    EXPECT_FALSE(f.has_all_of(TestFlag::A | TestFlag::D));
}

TEST(FlagsTest, HasAnyOfRequiresAny) {
    TestFlags f = TestFlag::A | TestFlag::B;
    EXPECT_TRUE(f.has_any_of(TestFlag::B | TestFlag::D));
    EXPECT_FALSE(f.has_any_of(TestFlag::C | TestFlag::D));
}

TEST(FlagsTest, CompoundOrAssignWorks) {
    TestFlags f = TestFlag::A;
    f |= TestFlag::B;
    EXPECT_TRUE(f.has(TestFlag::A));
    EXPECT_TRUE(f.has(TestFlag::B));
}

TEST(FlagsTest, ClearRemovesBit) {
    TestFlags f = TestFlag::A | TestFlag::B;
    f.clear(TestFlag::A);
    EXPECT_FALSE(f.has(TestFlag::A));
    EXPECT_TRUE(f.has(TestFlag::B));
}

TEST(FlagsTest, ToggleFlipsBit) {
    TestFlags f = TestFlag::A;
    f.toggle(TestFlag::A);
    EXPECT_FALSE(f.has(TestFlag::A));
    f.toggle(TestFlag::A);
    EXPECT_TRUE(f.has(TestFlag::A));
}

TEST(FlagsTest, AndMasksBits) {
    TestFlags f = TestFlag::A | TestFlag::B | TestFlag::C;
    TestFlags masked = f & (TestFlag::A | TestFlag::C);
    EXPECT_TRUE(masked.has(TestFlag::A));
    EXPECT_FALSE(masked.has(TestFlag::B));
    EXPECT_TRUE(masked.has(TestFlag::C));
}

TEST(FlagsTest, EqualityByRawBits) {
    TestFlags a = TestFlag::A | TestFlag::B;
    TestFlags b = TestFlag::B | TestFlag::A;
    EXPECT_EQ(a, b);
}

TEST(FlagsTest, RawReturnsBits) {
    TestFlags f = TestFlag::A | TestFlag::C;
    EXPECT_EQ(f.raw(), static_cast<std::underlying_type_t<TestFlag>>(TestFlag::A) |
                       static_cast<std::underlying_type_t<TestFlag>>(TestFlag::C));
}

TEST(NodeFlagsTest, SymbolicPrintingIncludesAllSetBits) {
    NodeFlags f = NodeFlag::Pure | NodeFlag::Effect;
    std::string s = to_string(f);
    EXPECT_NE(s.find("Pure"), std::string::npos);
    EXPECT_NE(s.find("Effect"), std::string::npos);
}

TEST(NodeFlagsTest, EmptyFlagsPrintsAsNone) {
    NodeFlags f;
    EXPECT_EQ(to_string(f), "(none)");
}

TEST(NodeFlagsTest, FlagBitNameReturnsSymbol) {
    EXPECT_EQ(flag_bit_name(NodeFlag::Pure), "Pure");
    EXPECT_EQ(flag_bit_name(NodeFlag::Effect), "Effect");
    EXPECT_EQ(flag_bit_name(NodeFlag::IsGuard), "IsGuard");
}
