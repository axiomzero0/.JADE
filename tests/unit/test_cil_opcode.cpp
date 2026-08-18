// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_cil_opcode.cpp
//
// Tests for the CIL opcode decoder (ECMA-335 subset).

#include <gtest/gtest.h>
#include "jade/cil/Opcode.hpp"

#include <vector>
#include <cstring>

using namespace jade;
using namespace jade::cil;

TEST(CilOpcodeTest, NopDecodesCorrectly) {
    uint8_t buf[] = {0x00};
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::Nop);
    EXPECT_EQ(d.length, 1);
}

TEST(CilOpcodeTest, LdI4ShortDecodesWithInt8Operand) {
    // ldc.i4.s 42
    uint8_t buf[] = {0x1F, 42};
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::LdI4_S);
    EXPECT_EQ(d.length, 2);
    EXPECT_EQ(d.operand_i32, 42);
}

TEST(CilOpcodeTest, LdI4DecodesWithInt32Operand) {
    // ldc.i4 0x12345678
    uint8_t buf[] = {0x20, 0x78, 0x56, 0x34, 0x12};
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::LdI4);
    EXPECT_EQ(d.length, 5);
    EXPECT_EQ(d.operand_i32, 0x12345678);
}

TEST(CilOpcodeTest, LdI8DecodesWithInt64Operand) {
    // ldc.i8 0x123456789ABCDEF0
    uint8_t buf[] = {0x21, 0xF0, 0xDE, 0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12};
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::LdI8);
    EXPECT_EQ(d.length, 9);
    EXPECT_EQ(d.operand_i64, 0x123456789ABCDEF0LL);
}

TEST(CilOpcodeTest, LdR8DecodesWithDoubleOperand) {
    // ldc.r8 3.14
    union { double d; uint8_t b[8]; } u;
    u.d = 3.14;
    uint8_t buf[9] = {0x23};
    std::memcpy(buf + 1, u.b, 8);
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::LdR8);
    EXPECT_EQ(d.length, 9);
    EXPECT_DOUBLE_EQ(d.operand_r8, 3.14);
}

TEST(CilOpcodeTest, ShortBranchDecodesWithInt8Target) {
    // br.s +10
    uint8_t buf[] = {0x2B, 10};
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::Br_S);
    EXPECT_EQ(d.length, 2);
    EXPECT_EQ(d.operand_i32, 10);
}

TEST(CilOpcodeTest, LongBranchDecodesWithInt32Target) {
    // br +1000
    uint8_t buf[] = {0x38, 0xE8, 0x03, 0x00, 0x00};
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::Br);
    EXPECT_EQ(d.length, 5);
    EXPECT_EQ(d.operand_i32, 1000);
}

TEST(CilOpcodeTest, LdStrDecodesWithTokenOperand) {
    // ldstr 0x06000001 (method token)
    uint8_t buf[] = {0x72, 0x01, 0x00, 0x00, 0x06};
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::LdStr);
    EXPECT_EQ(d.length, 5);
    EXPECT_EQ(d.operand_u32, 0x06000001u);
}

TEST(CilOpcodeTest, AddDecodesWithNoOperand) {
    uint8_t buf[] = {0x58};
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::Add);
    EXPECT_EQ(d.length, 1);
}

TEST(CilOpcodeTest, RetDecodesWithNoOperand) {
    uint8_t buf[] = {0x2A};
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::Ret);
    EXPECT_EQ(d.length, 1);
}

TEST(CilOpcodeTest, TwoByteOpcodeCeqDecodes) {
    // ceq = 0xFE 0x01
    uint8_t buf[] = {0xFE, 0x01};
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::Ceq);
    EXPECT_EQ(d.length, 2);
}

TEST(CilOpcodeTest, TwoByteOpcodeLdlocDecodesWithUint16) {
    // ldloc 0x0102 = 0xFE 0x0C 0x02 0x01
    uint8_t buf[] = {0xFE, 0x0C, 0x02, 0x01};
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::LdLoc);
    EXPECT_EQ(d.length, 4);
    EXPECT_EQ(d.operand_u32, 0x0102u);
}

TEST(CilOpcodeTest, ConstrainedPrefixDecodesWithTypeToken) {
    // constrained. 0x02000001 = 0xFE 0x16 0x01 0x00 0x00 0x02
    uint8_t buf[] = {0xFE, 0x16, 0x01, 0x00, 0x00, 0x02};
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::Constrained);
    EXPECT_EQ(d.length, 6);
    EXPECT_EQ(d.operand_u32, 0x02000001u);
}

TEST(CilOpcodeTest, OpcodeInfoSaysCallIsCallAndCanThrow) {
    const auto& info = opcode_info(CilOpcode::Call);
    EXPECT_TRUE(info.is_call);
    EXPECT_TRUE(info.can_throw);
}

TEST(CilOpcodeTest, OpcodeInfoSaysAddIsPure) {
    const auto& info = opcode_info(CilOpcode::Add);
    EXPECT_FALSE(info.is_call);
    EXPECT_FALSE(info.can_throw);
    EXPECT_TRUE(info.loads_value);
    EXPECT_TRUE(info.stores_value);
}

TEST(CilOpcodeTest, OpcodeInfoSaysThrowCanThrow) {
    const auto& info = opcode_info(CilOpcode::Throw);
    EXPECT_TRUE(info.can_throw);
}

TEST(CilOpcodeTest, OpcodeNameIsCorrect) {
    EXPECT_EQ(opcode_name(CilOpcode::Add), "add");
    EXPECT_EQ(opcode_name(CilOpcode::LdStr), "ldstr");
    EXPECT_EQ(opcode_name(CilOpcode::CallVirt), "callvirt");
    EXPECT_EQ(opcode_name(CilOpcode::Ceq), "ceq");
}

TEST(CilOpcodeTest, InvalidByteReturnsInvalid) {
    uint8_t buf[] = {0xFF};  // not a valid opcode
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::Invalid);
}

TEST(CilOpcodeTest, TruncatedBufferReturnsInvalid) {
    uint8_t buf[] = {0x20};  // ldc.i4 but no operand bytes
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::Invalid);
}

TEST(CilOpcodeTest, SwitchDecodesCountAndSkipsTargets) {
    // switch N=2 target1 target2  → 5 + 2*4 = 13 bytes
    uint8_t buf[] = {
        0x45,                                    // switch
        0x02, 0x00, 0x00, 0x00,                  // N = 2
        0x10, 0x00, 0x00, 0x00,                  // target1 = 16
        0x20, 0x00, 0x00, 0x00,                  // target2 = 32
    };
    auto d = decode_opcode(buf, sizeof(buf));
    EXPECT_EQ(d.op, CilOpcode::Switch);
    EXPECT_EQ(d.length, 13);
    EXPECT_EQ(d.operand_u32, 2u);  // count
}
