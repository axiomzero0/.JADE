// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_jvm_opcode.cpp
//
// Tests for the JVM opcode decoder (full JVMS §6.5).

#include <gtest/gtest.h>
#include "jade/jvm/Opcode.hpp"

#include <vector>
#include <cstring>

using namespace jade;
using namespace jade::jvm;

namespace {

class JvmEncoder {
public:
    JvmEncoder& emit1(uint8_t op)               { bytes_.push_back(op); return *this; }
    JvmEncoder& emit_u1(uint8_t op, uint8_t v)  {
        bytes_.push_back(op);
        bytes_.push_back(v);
        return *this;
    }
    JvmEncoder& emit_s1(uint8_t op, int8_t v) {
        return emit_u1(op, static_cast<uint8_t>(v));
    }
    JvmEncoder& emit_u2(uint8_t op, uint16_t v) {
        bytes_.push_back(op);
        bytes_.push_back(static_cast<uint8_t>(v >> 8));
        bytes_.push_back(static_cast<uint8_t>(v & 0xFF));
        return *this;
    }
    JvmEncoder& emit_s2(uint8_t op, int16_t v) {
        return emit_u2(op, static_cast<uint16_t>(v));
    }
    JvmEncoder& emit_s4(uint8_t op, int32_t v) {
        // JVM bytecode is big-endian (JVMS §4.10.1).
        bytes_.push_back(op);
        bytes_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        bytes_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        bytes_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        bytes_.push_back(static_cast<uint8_t>(v & 0xFF));
        return *this;
    }
    JvmEncoder& emit_u1u1(uint8_t op, uint8_t a, uint8_t b) {
        bytes_.push_back(op);
        bytes_.push_back(a);
        bytes_.push_back(b);
        return *this;
    }
    JvmEncoder& emit_raw(uint8_t b) { bytes_.push_back(b); return *this; }

    [[nodiscard]] std::vector<uint8_t> build() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};

}  // namespace

TEST(JvmOpcodeTest, NopDecodesCorrectly) {
    auto bytes = JvmEncoder().emit1(0x00).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Nop);
    EXPECT_EQ(d.length, 1);
}

TEST(JvmOpcodeTest, Iconst5DecodesAsIntConstant) {
    auto bytes = JvmEncoder().emit1(0x08).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Iconst5);
    EXPECT_EQ(d.length, 1);
}

TEST(JvmOpcodeTest, BipushDecodesWithSignedByte) {
    auto bytes = JvmEncoder().emit_s1(0x10, -42).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Bipush);
    EXPECT_EQ(d.length, 2);
    EXPECT_EQ(d.operand_i32, -42);
}

TEST(JvmOpcodeTest, SipushDecodesWithSignedShort) {
    auto bytes = JvmEncoder().emit_s2(0x11, 1000).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Sipush);
    EXPECT_EQ(d.length, 3);
    EXPECT_EQ(d.operand_i32, 1000);
}

TEST(JvmOpcodeTest, LdcDecodesWithU1Index) {
    auto bytes = JvmEncoder().emit_u1(0x12, 42).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Ldc);
    EXPECT_EQ(d.operand_u32, 42u);
}

TEST(JvmOpcodeTest, IloadDecodesWithLocalIndex) {
    auto bytes = JvmEncoder().emit_u1(0x15, 5).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Iload);
    EXPECT_EQ(d.operand_u32, 5u);
}

TEST(JvmOpcodeTest, Iload0HasNoOperand) {
    auto bytes = JvmEncoder().emit1(0x1A).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Iload0);
    EXPECT_EQ(d.length, 1);
}

TEST(JvmOpcodeTest, GotoDecodesWithS2Offset) {
    auto bytes = JvmEncoder().emit_s2(0xA7, 100).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Goto);
    EXPECT_EQ(d.length, 3);
    EXPECT_EQ(d.operand_i32, 100);
}

TEST(JvmOpcodeTest, GotoWDecodesWithS4Offset) {
    auto bytes = JvmEncoder().emit_s4(0xC8, 100000).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::GotoW);
    EXPECT_EQ(d.length, 5);
    EXPECT_EQ(d.operand_i32, 100000);
}

TEST(JvmOpcodeTest, IincDecodesWithLocalAndConst) {
    auto bytes = JvmEncoder().emit_u1u1(0x84, 3, 5).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Iinc);
    EXPECT_EQ(d.length, 3);
    EXPECT_EQ(d.operand_u32, 3u);   // local index
    EXPECT_EQ(d.operand_i32, 5);    // const value
}

TEST(JvmOpcodeTest, InvokevirtualDecodesWithCpIndex) {
    auto bytes = JvmEncoder().emit_u2(0xB6, 0x1234).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Invokevirtual);
    EXPECT_EQ(d.operand_u32, 0x1234u);
    EXPECT_EQ(d.length, 3);
}

TEST(JvmOpcodeTest, InvokeinterfaceDecodesWith4Bytes) {
    // invokeinterface: u2 index + u1 count + u1 zero
    auto bytes = JvmEncoder().emit_u2(0xB9, 0x0005)
                    .emit_raw(2)
                    .emit_raw(0)
                    .build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Invokeinterface);
    EXPECT_EQ(d.operand_u32, 5u);
    EXPECT_EQ(d.length, 5);
}

TEST(JvmOpcodeTest, InvokedynamicDecodesWith4Bytes) {
    auto bytes = JvmEncoder().emit_u2(0xBA, 0x0007)
                    .emit_raw(0)
                    .emit_raw(0)
                    .build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Invokedynamic);
    EXPECT_EQ(d.operand_u32, 7u);
    EXPECT_EQ(d.length, 5);
}

TEST(JvmOpcodeTest, NewDecodesWithClassIndex) {
    auto bytes = JvmEncoder().emit_u2(0xBB, 0x0002).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::New);
    EXPECT_EQ(d.operand_u32, 2u);
}

TEST(JvmOpcodeTest, NewarrayDecodesWithAtype) {
    auto bytes = JvmEncoder().emit_u1(0xBC, 10).build();  // 10 = int[]
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Newarray);
    EXPECT_EQ(d.operand_u32, 10u);
    EXPECT_EQ(newarray_type_name(NewArrayType::Int), "int[]");
}

TEST(JvmOpcodeTest, MultianewarrayDecodesWithIndexAndDim) {
    auto bytes = JvmEncoder().emit_u2(0xC5, 0x0003).emit_raw(2).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Multianewarray);
    EXPECT_EQ(d.operand_u32, 3u);
    EXPECT_EQ(d.switch_count, 2);  // 2 dimensions
    EXPECT_EQ(d.length, 4);
}

TEST(JvmOpcodeTest, CheckcastDecodesWithClassIndex) {
    auto bytes = JvmEncoder().emit_u2(0xC0, 0x0004).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Checkcast);
    EXPECT_EQ(d.operand_u32, 4u);
}

TEST(JvmOpcodeTest, MonitorenterHasNoOperand) {
    auto bytes = JvmEncoder().emit1(0xC2).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Monitorenter);
    EXPECT_EQ(d.length, 1);
}

TEST(JvmOpcodeTest, WideIloadDecodesWithU2LocalIndex) {
    // wide + iload + u2 index (big-endian: 0x00 0x05 = local 5)
    std::vector<uint8_t> raw = {0xC4, 0x15, 0x00, 0x05};
    auto d = decode_opcode(raw.data(), raw.size());
    EXPECT_EQ(d.op, JvmOpcode::WideIload);
    EXPECT_EQ(d.length, 4);
    EXPECT_EQ(d.operand_u32, 5u);
}

TEST(JvmOpcodeTest, WideIincDecodesWithU2LocalAndS2Const) {
    // wide + iinc + u2 local + u2 const (big-endian)
    // local = 0x00 0x05 = 5, const = 0xFF 0xFF = -1
    std::vector<uint8_t> raw = {0xC4, 0x84, 0x00, 0x05, 0xFF, 0xFF};
    auto d = decode_opcode(raw.data(), raw.size());
    EXPECT_EQ(d.op, JvmOpcode::WideIinc);
    EXPECT_EQ(d.length, 6);
    EXPECT_EQ(d.operand_u32, 5u);
    EXPECT_EQ(d.operand_i32, -1);
}

TEST(JvmOpcodeTest, IfnullDecodesWithS2Offset) {
    auto bytes = JvmEncoder().emit_s2(0xC6, 50).build();
    auto d = decode_opcode(bytes.data(), bytes.size());
    EXPECT_EQ(d.op, JvmOpcode::Ifnull);
    EXPECT_EQ(d.operand_i32, 50);
}

TEST(JvmOpcodeTest, OpcodeInfoSaysInvokevirtualIsCall) {
    const auto& info = opcode_info(JvmOpcode::Invokevirtual);
    EXPECT_TRUE(info.is_call);
    EXPECT_TRUE(info.can_throw);
}

TEST(JvmOpcodeTest, OpcodeInfoSaysIaddIsPure) {
    const auto& info = opcode_info(JvmOpcode::Iadd);
    EXPECT_FALSE(info.is_call);
    EXPECT_FALSE(info.can_throw);
    EXPECT_TRUE(info.loads_value);
    EXPECT_TRUE(info.stores_value);
}

TEST(JvmOpcodeTest, OpcodeInfoSaysAthrowCanThrow) {
    const auto& info = opcode_info(JvmOpcode::Athrow);
    EXPECT_TRUE(info.can_throw);
}

TEST(JvmOpcodeTest, OpcodeNameIsCorrect) {
    EXPECT_EQ(opcode_name(JvmOpcode::Iadd), "iadd");
    EXPECT_EQ(opcode_name(JvmOpcode::Invokevirtual), "invokevirtual");
    EXPECT_EQ(opcode_name(JvmOpcode::Monitorenter), "monitorenter");
    EXPECT_EQ(opcode_name(JvmOpcode::Invokedynamic), "invokedynamic");
}

TEST(JvmOpcodeTest, InvalidByteReturnsInvalid) {
    std::vector<uint8_t> raw = {0xCB};   // reserved
    auto d = decode_opcode(raw.data(), raw.size());
    EXPECT_EQ(d.op, JvmOpcode::Invalid);
}

TEST(JvmOpcodeTest, TruncatedIloadReturnsInvalid) {
    std::vector<uint8_t> raw = {0x15};   // iload but no operand
    auto d = decode_opcode(raw.data(), raw.size());
    EXPECT_EQ(d.op, JvmOpcode::Invalid);
}

TEST(JvmOpcodeTest, AllArithmeticOpcodesAreKnown) {
    // Sanity check: walk the entire 0x00..0xFF range and verify every
    // opcode in the arithmetic range (0x60..0x83) is recognized.
    for (uint16_t v = 0x60; v <= 0x83; ++v) {
        std::vector<uint8_t> raw = {static_cast<uint8_t>(v)};
        auto d = decode_opcode(raw.data(), raw.size());
        // Most arithmetic opcodes are 1-byte with no operand.
        if (v == 0x84) continue;  // iinc is special
        EXPECT_NE(d.op, JvmOpcode::Invalid)
            << "opcode 0x" << std::hex << v << " should be valid";
    }
}

TEST(JvmOpcodeTest, NewarrayTypeNameForAllTypes) {
    EXPECT_EQ(newarray_type_name(NewArrayType::Boolean), "boolean[]");
    EXPECT_EQ(newarray_type_name(NewArrayType::Char), "char[]");
    EXPECT_EQ(newarray_type_name(NewArrayType::Float), "float[]");
    EXPECT_EQ(newarray_type_name(NewArrayType::Double), "double[]");
    EXPECT_EQ(newarray_type_name(NewArrayType::Byte), "byte[]");
    EXPECT_EQ(newarray_type_name(NewArrayType::Short), "short[]");
    EXPECT_EQ(newarray_type_name(NewArrayType::Int), "int[]");
    EXPECT_EQ(newarray_type_name(NewArrayType::Long), "long[]");
}
