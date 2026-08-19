// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_granit_cil_interpreter.cpp
//
// Tests for the real CIL bytecode interpreter.

#include <gtest/gtest.h>
#include "jade/tier0_granit/CilInterpreter.hpp"
#include "jade/cil/Opcode.hpp"

#include <vector>
#include <cstdint>

using namespace jade;
using namespace jade::granit;

namespace {

class CilEncoder {
public:
    CilEncoder& emit1(uint8_t op)               { bytes_.push_back(op); return *this; }
    CilEncoder& emit_u1(uint8_t op, uint8_t v)  {
        bytes_.push_back(op);
        bytes_.push_back(v);
        return *this;
    }
    CilEncoder& emit_s1(uint8_t op, int8_t v) { return emit_u1(op, static_cast<uint8_t>(v)); }
    CilEncoder& emit_int32(uint8_t op, int32_t v) {
        bytes_.push_back(op);
        for (int i = 0; i < 4; ++i) {
            bytes_.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        }
        return *this;
    }
    CilEncoder& emit_raw(uint8_t b) { bytes_.push_back(b); return *this; }

    [[nodiscard]] std::vector<uint8_t> build() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};

}  // namespace

TEST(CilInterpreterTest, LdcI4AndRet) {
    // ldc.i4 42; ret
    auto bytes = CilEncoder().emit_int32(0x20, 42).emit1(0x2A).build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(std::get<int32_t>(*r), 42);
}

TEST(CilInterpreterTest, LdcI4ShortForms) {
    // ldc.i4.0; ldc.i4.1; ...; ldc.i4.8; ldc.i4.m1
    auto bytes = CilEncoder()
                     .emit1(0x16)   // ldc.i4.0
                     .emit1(0x17)   // ldc.i4.1
                     .emit1(0x2A)   // ret (returns top = 1)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 1);
}

TEST(CilInterpreterTest, AddTwoIntegers) {
    // ldc.i4 3; ldc.i4 4; add; ret
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 3)
                     .emit_int32(0x20, 4)
                     .emit1(0x58)   // add
                     .emit1(0x2A)   // ret
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 7);
}

TEST(CilInterpreterTest, SubtractTwoIntegers) {
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 10)
                     .emit_int32(0x20, 4)
                     .emit1(0x59)   // sub
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 6);
}

TEST(CilInterpreterTest, MultiplyTwoIntegers) {
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 6)
                     .emit_int32(0x20, 7)
                     .emit1(0x5A)   // mul
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 42);
}

TEST(CilInterpreterTest, DivideTwoIntegers) {
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 20)
                     .emit_int32(0x20, 4)
                     .emit1(0x5B)   // div
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 5);
}

TEST(CilInterpreterTest, DivideByZeroThrows) {
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 10)
                     .emit_int32(0x20, 0)
                     .emit1(0x5B)   // div
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    EXPECT_FALSE(r.has_value());
}

TEST(CilInterpreterTest, LdLocStLocRoundTrip) {
    // ldc.i4 99; stloc.0; ldloc.0; ret
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 99)
                     .emit1(0x0A)   // stloc.0
                     .emit1(0x06)   // ldloc.0
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 1, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 99);
}

TEST(CilInterpreterTest, LdArg) {
    // ldarg.0; ret
    auto bytes = CilEncoder().emit1(0x02).emit1(0x2A).build();
    CilInterpreter interp;
    std::vector<Value> args = {Value{int32_t{777}}};
    auto r = interp.run(bytes, 0, 1, std::move(args));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 777);
}

TEST(CilInterpreterTest, BitwiseAnd) {
    // ldc.i4 0xFF; ldc.i4 0x0F; and; ret
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 0xFF)
                     .emit_int32(0x20, 0x0F)
                     .emit1(0x5F)   // and
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 0x0F);
}

TEST(CilInterpreterTest, BitwiseOr) {
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 0xF0)
                     .emit_int32(0x20, 0x0F)
                     .emit1(0x60)   // or
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 0xFF);
}

TEST(CilInterpreterTest, ShiftLeft) {
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 1)
                     .emit_int32(0x20, 4)
                     .emit1(0x62)   // shl
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 16);
}

TEST(CilInterpreterTest, CeqReturns1OnEqual) {
    // ldc.i4 5; ldc.i4 5; ceq; ret
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 5)
                     .emit_int32(0x20, 5)
                     .emit1(0xFE).emit1(0x01)   // ceq
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 1);
}

TEST(CilInterpreterTest, CeqReturns0OnNotEqual) {
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 5)
                     .emit_int32(0x20, 6)
                     .emit1(0xFE).emit1(0x01)   // ceq
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 0);
}

TEST(CilInterpreterTest, BrfalseTaken) {
    // ldc.i4 0; brfalse.s +6; ldc.i4 99; ret; ldc.i4 42; ret
    // Since 0 is falsy, brfalse IS taken → skips to ldc.i4 42.
    // Layout:
    //   0-4: ldc.i4 0         (5 bytes)
    //   5-6: brfalse.s +6    (2 bytes; target = 7 + 6 = 13)
    //   7-11: ldc.i4 99      (5 bytes, skipped)
    //   12: ret               (1 byte, skipped)
    //   13-17: ldc.i4 42     (5 bytes, branch target)
    //   18: ret
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 0)        // 0-4: ldc.i4 0
                     .emit_s1(0x2C, 6)           // 5-6: brfalse.s +6 → pc=13
                     .emit_int32(0x20, 99)       // 7-11: ldc.i4 99 (skipped)
                     .emit1(0x2A)                // 12: ret (skipped)
                     .emit_int32(0x20, 42)       // 13-17: ldc.i4 42 (branch target)
                     .emit1(0x2A)                // 18: ret
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 42);
}

TEST(CilInterpreterTest, BrfalseNotTaken) {
    // ldc.i4 1; brfalse.s +6; ldc.i4 99; ret; ldc.i4 0; ret
    // Since 1 is truthy, brfalse is NOT taken → returns 99.
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 1)        // 0-4: ldc.i4 1
                     .emit_s1(0x2C, 6)           // 5-6: brfalse.s +6 → pc=13
                     .emit_int32(0x20, 99)       // 7-11: ldc.i4 99 (taken if not branched)
                     .emit1(0x2A)                // 12: ret
                     .emit_int32(0x20, 0)        // 13-17: ldc.i4 0 (branch target, not taken)
                     .emit1(0x2A)                // 18: ret
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 99);
}

TEST(CilInterpreterTest, Dup) {
    // ldc.i4 5; dup; add; ret → 5+5=10
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 5)
                     .emit1(0x25)   // dup
                     .emit1(0x58)   // add
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 10);
}

TEST(CilInterpreterTest, ConvI8) {
    // ldc.i4 42; conv.i8; ret
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 42)
                     .emit1(0x6A)   // conv.i8
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int64_t>(*r), 42LL);
}

TEST(CilInterpreterTest, ChainedArithmetic) {
    // (3 + 4) * 5 = 35
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 3)
                     .emit_int32(0x20, 4)
                     .emit1(0x58)   // add → 7
                     .emit_int32(0x20, 5)
                     .emit1(0x5A)   // mul → 35
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), 35);
}

TEST(CilInterpreterTest, LdcI4SCoversNegative) {
    // ldc.i4.s -1; ret
    auto bytes = CilEncoder().emit_s1(0x1F, -1).emit1(0x2A).build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), -1);
}

TEST(CilInterpreterTest, Neg) {
    auto bytes = CilEncoder()
                     .emit_int32(0x20, 5)
                     .emit1(0x65)   // neg
                     .emit1(0x2A)
                     .build();
    CilInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<int32_t>(*r), -5);
}
