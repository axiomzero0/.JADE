// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_granit_jvm_interpreter.cpp
//
// Tests for the real JVM bytecode interpreter.

#include <gtest/gtest.h>
#include "jade/tier0_granit/JvmInterpreter.hpp"
#include "jade/jvm/Opcode.hpp"

#include <vector>
#include <cstdint>

using namespace jade;
using namespace jade::granit;

namespace {

class JvmEncoder {
public:
    JvmEncoder& emit1(uint8_t op)               { bytes_.push_back(op); return *this; }
    JvmEncoder& emit_u1(uint8_t op, uint8_t v)  {
        bytes_.push_back(op);
        bytes_.push_back(v);
        return *this;
    }
    JvmEncoder& emit_s1(uint8_t op, int8_t v) { return emit_u1(op, static_cast<uint8_t>(v)); }
    JvmEncoder& emit_u2(uint8_t op, uint16_t v) {
        bytes_.push_back(op);
        bytes_.push_back(static_cast<uint8_t>(v & 0xFF));
        bytes_.push_back(static_cast<uint8_t>(v >> 8));
        return *this;
    }
    JvmEncoder& emit_s2(uint8_t op, int16_t v) {
        return emit_u2(op, static_cast<uint16_t>(v));
    }
    JvmEncoder& emit_raw(uint8_t b) { bytes_.push_back(b); return *this; }

    [[nodiscard]] std::vector<uint8_t> build() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};

}  // namespace

TEST(JvmInterpreterTest, Iconst5AndIReturn) {
    // iconst_5; ireturn
    auto bytes = JvmEncoder().emit1(0x08).emit1(0xAC).build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(r->as_int32(), 5);
}

TEST(JvmInterpreterTest, IaddTwoConstants) {
    // iconst_2; iconst_3; iadd; ireturn
    auto bytes = JvmEncoder()
                     .emit1(0x05)   // iconst_2
                     .emit1(0x06)   // iconst_3
                     .emit1(0x60)   // iadd
                     .emit1(0xAC)   // ireturn
                     .build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int32(), 5);
}

TEST(JvmInterpreterTest, IsubTwoConstants) {
    auto bytes = JvmEncoder()
                     .emit1(0x08)   // iconst_5
                     .emit1(0x06)   // iconst_3
                     .emit1(0x64)   // isub
                     .emit1(0xAC)
                     .build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int32(), 2);
}

TEST(JvmInterpreterTest, ImulTwoConstants) {
    auto bytes = JvmEncoder()
                     .emit1(0x05)   // iconst_2
                     .emit1(0x06)   // iconst_3
                     .emit1(0x68)   // imul
                     .emit1(0xAC)
                     .build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int32(), 6);
}

TEST(JvmInterpreterTest, IdivByZeroThrows) {
    auto bytes = JvmEncoder()
                     .emit1(0x08)   // iconst_5
                     .emit1(0x03)   // iconst_0
                     .emit1(0x6C)   // idiv
                     .emit1(0xAC)
                     .build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    EXPECT_FALSE(r.has_value());
}

TEST(JvmInterpreterTest, IloadIstoreRoundTrip) {
    // iconst_5; istore_0; iload_0; ireturn
    auto bytes = JvmEncoder()
                     .emit1(0x08)   // iconst_5
                     .emit1(0x3B)   // istore_0
                     .emit1(0x1A)   // iload_0
                     .emit1(0xAC)   // ireturn
                     .build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 1, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int32(), 5);
}

TEST(JvmInterpreterTest, Iinc) {
    // iconst_0; istore_0; iinc 0 5; iload_0; ireturn
    auto bytes = JvmEncoder()
                     .emit1(0x03)              // iconst_0
                     .emit1(0x3B)              // istore_0
                     .emit_u1(0x84, 0).emit_raw(5)  // iinc 0 5
                     .emit1(0x1A)              // iload_0
                     .emit1(0xAC)
                     .build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 1, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int32(), 5);
}

TEST(JvmInterpreterTest, BitwiseAnd) {
    auto bytes = JvmEncoder()
                     .emit1(0x08)   // iconst_5 (0b101)
                     .emit1(0x06)   // iconst_3 (0b011)
                     .emit1(0x7E)   // iand
                     .emit1(0xAC)
                     .build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int32(), 1);   // 5 & 3 = 1
}

TEST(JvmInterpreterTest, IfneBranchTaken) {
    // iconst_1; ifne +5; iconst_0; ireturn; iconst_1; ireturn
    // Since 1 != 0, ifne is taken → skips to "iconst_1; ireturn" → returns 1.
    auto bytes = JvmEncoder()
                     .emit1(0x04)              // 0: iconst_1
                     .emit_s2(0x9A, 5)        // 1-3: ifne +5 → pc=8 (offset from next instr at pc=3)
                     .emit1(0x03)              // 4: iconst_0 (not taken path)
                     .emit1(0xAC)              // 5: ireturn (returns 0)
                     .emit1(0x04)              // 6: iconst_1 (taken path)
                     .emit1(0xAC)              // 7: ireturn (returns 1)
                     .build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int32(), 1);
}

TEST(JvmInterpreterTest, Dup) {
    // iconst_5; dup; iadd; ireturn → 5+5=10
    auto bytes = JvmEncoder()
                     .emit1(0x08)   // iconst_5
                     .emit1(0x59)   // dup
                     .emit1(0x60)   // iadd
                     .emit1(0xAC)
                     .build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int32(), 10);
}

TEST(JvmInterpreterTest, Bipush) {
    // bipush 42; ireturn
    auto bytes = JvmEncoder().emit_s1(0x10, 42).emit1(0xAC).build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int32(), 42);
}

TEST(JvmInterpreterTest, ChainedArithmetic) {
    // (2 + 3) * 4 = 20
    auto bytes = JvmEncoder()
                     .emit1(0x05)   // iconst_2
                     .emit1(0x06)   // iconst_3
                     .emit1(0x60)   // iadd → 5
                     .emit1(0x07)   // iconst_4
                     .emit1(0x68)   // imul → 20
                     .emit1(0xAC)
                     .build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int32(), 20);
}

TEST(JvmInterpreterTest, LconstAndLreturn) {
    // lconst_1; lreturn
    auto bytes = JvmEncoder().emit1(0x0A).emit1(0xAD).build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int64(), 1LL);
}

TEST(JvmInterpreterTest, LcmpEqualReturns0) {
    // lconst_1; lconst_1; lcmp; ireturn → 0 (equal)
    auto bytes = JvmEncoder()
                     .emit1(0x0A)   // lconst_1
                     .emit1(0x0A)   // lconst_1
                     .emit1(0x94)   // lcmp
                     .emit1(0xAC)
                     .build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int32(), 0);
}

TEST(JvmInterpreterTest, Ineg) {
    // iconst_5; ineg; ireturn → -5
    auto bytes = JvmEncoder().emit1(0x08).emit1(0x74).emit1(0xAC).build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int32(), -5);
}

TEST(JvmInterpreterTest, VoidReturn) {
    // return (void)
    auto bytes = JvmEncoder().emit1(0xB1).build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    // Void return produces monostate.
    EXPECT_TRUE(r->is_uninit());
}

TEST(JvmInterpreterTest, I2lConversion) {
    // iconst_5; i2l; lreturn
    auto bytes = JvmEncoder().emit1(0x08).emit1(0x85).emit1(0xAD).build();
    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->as_int64(), 5LL);
}
