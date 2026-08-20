// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_regression_jvm_endian.cpp
//
// Regression tests for Rule 36 (5 regression tests per bug fix).
//
// Bug 006: JVM bytecode multi-byte operands decoded with wrong endianness.
//
// JVM bytecode is big-endian (JVMS §4.10.1). The decoder used memcpy which
// is native-endian (little-endian on x86), causing branch offsets and
// constant pool indices to be wrong by a factor of 256.

#include <gtest/gtest.h>
#include "jade/jvm/Opcode.hpp"
#include "jade/tier0_granit/JvmInterpreter.hpp"
#include "jade/tier0_granit/Value.hpp"

#include <vector>
#include <cstdint>

using namespace jade;
using namespace jade::jvm;
using namespace jade::granit;

namespace {

// Big-endian encoder (correct JVM bytecode format).
class BEEnc {
public:
    BEEnc& emit1(uint8_t op) { bytes_.push_back(op); return *this; }
    BEEnc& emit_u1(uint8_t op, uint8_t v) { bytes_.push_back(op); bytes_.push_back(v); return *this; }
    BEEnc& emit_s1(uint8_t op, int8_t v) { bytes_.push_back(op); bytes_.push_back(static_cast<uint8_t>(v)); return *this; }
    BEEnc& emit_u2(uint8_t op, uint16_t v) {
        // Big-endian: high byte first.
        bytes_.push_back(op);
        bytes_.push_back(static_cast<uint8_t>(v >> 8));
        bytes_.push_back(static_cast<uint8_t>(v & 0xFF));
        return *this;
    }
    BEEnc& emit_s2(uint8_t op, int16_t v) { return emit_u2(op, static_cast<uint16_t>(v)); }
    [[nodiscard]] std::vector<uint8_t> build() const { return bytes_; }
private:
    std::vector<uint8_t> bytes_;
};

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Bug 006: JVM big-endian decoding
// ═══════════════════════════════════════════════════════════════════════════════

// ── 006.1: Minimal reproducer — branch offset decoded correctly ──────────────
// iconst_1; ifne +6; iconst_0; ireturn; nop; iconst_1; ireturn
// JVM branch offsets are relative to the branch OPCODE's address.
// ifne at pc=1, offset=6 → target = 1 + 6 = 7.
// The interpreter does frame.pc += offset (before pc += length), so
// pc=1 + 6 = 7. At pc=7: iconst_1; ireturn → returns 1.
TEST(Regression006JvmBigEndian, MinimalReproducerBranchOffset) {
    auto bytes = BEEnc()
                     .emit1(0x04)              // 0: iconst_1
                     .emit_s2(0x9A, 6)        // 1-3: ifne +6 → target=pc+6=7
                     .emit1(0x03)              // 4: iconst_0
                     .emit1(0xAC)              // 5: ireturn (returns 0)
                     .emit1(0x00)              // 6: nop
                     .emit1(0x04)              // 7: iconst_1
                     .emit1(0xAC)              // 8: ireturn (returns 1)
                     .build();

    JvmInterpreter interp;
    auto r = interp.run(bytes, 0, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // 1 != 0 → ifne taken → returns 1.
    EXPECT_EQ(r->as_int32(), 1)
        << "ifne branch must be taken (1 != 0)";
}

// ── 006.2: Variant trigger — constant pool index decoded correctly ──────────
// new + putfield + getfield + ireturn
// The constant pool indices (0x0004, 0x0004) must decode as 4, not 1024.
TEST(Regression006JvmBigEndian, VariantTriggerCpIndex) {
    // new #2; dup; bipush 42; putfield #4; getfield #4; ireturn
    std::vector<uint8_t> raw = {
        0xBB, 0x00, 0x02,    // new #2
        0x59,                 // dup
        0x10, 42,             // bipush 42
        0xB5, 0x00, 0x04,    // putfield #4
        0xB4, 0x00, 0x04,    // getfield #4
        0xAC,                 // ireturn
    };

    // Just verify the opcode decoder reads the CP index as 4 (not 1024).
    // putfield is at offset 6.
    auto d = decode_opcode(raw.data() + 6, raw.size() - 6);
    EXPECT_EQ(d.op, JvmOpcode::Putfield);
    EXPECT_EQ(d.operand_u32, 4u)
        << "CP index must be 4 (big-endian), not 1024 (little-endian)";

    // getfield is at offset 9.
    auto d2 = decode_opcode(raw.data() + 9, raw.size() - 9);
    EXPECT_EQ(d2.op, JvmOpcode::Getfield);
    EXPECT_EQ(d2.operand_u32, 4u);
}

// ── 006.3: Boundary/negative — negative offset (backward branch) ─────────────
// Ensures the fix handles negative S2 offsets (sign-extension).
// A backward goto with offset -3 should jump back 3 bytes.
TEST(Regression006JvmBigEndian, BoundaryNegativeNegativeOffset) {
    // Test that a negative S2 decodes correctly.
    // 0xFF 0xFD in big-endian = 0xFFFD = -3 (sign-extended int16).
    std::vector<uint8_t> raw = {0xA7, 0xFF, 0xFD};  // goto -3
    auto d = decode_opcode(raw.data(), raw.size());
    EXPECT_EQ(d.op, JvmOpcode::Goto);
    EXPECT_EQ(d.operand_i32, -3)
        << "Negative S2 offset must be -3 (big-endian sign-extended)";
}

// ── 006.4: Integration/contextual — loop with if_icmpge and goto ──────────────
// A real loop: sum = 0; for (i=0; i<5; i++) sum += i; return sum;
// This exercises both forward (if_icmpge) and backward (goto) branches
// with big-endian offsets.
TEST(Regression006JvmBigEndian, IntegrationContextualLoop) {
    // Hand-assembled bytecode for:
    //   sum = 0; i = 0;
    //   loop: if i >= 5 goto end; sum += i; i++; goto loop; end: return sum;
    //
    // JVM branch offsets are relative to the branch OPCODE's address.
    // if_icmpge at pc=6, target=pc=19 → offset = 19-6 = 13.
    // goto at pc=16, target=pc=4 (loop header) → offset = 4-16 = -12.
    std::vector<uint8_t> raw = {
        0x03,                 // 0: iconst_0
        0x3C,                 // 1: istore_1
        0x03,                 // 2: iconst_0
        0x3D,                 // 3: istore_2
        0x1C,                 // 4: iload_2        ← loop header
        0x08,                 // 5: iconst_5
        0xA2, 0x00, 0x0D,    // 6: if_icmpge +13 → target=6+13=19
        0x1B,                 // 9: iload_1
        0x1C,                 // 10: iload_2
        0x60,                 // 11: iadd
        0x3C,                 // 12: istore_1
        0x84, 0x02, 0x01,    // 13: iinc 2, 1
        0xA7, 0xFF, 0xF4,    // 16: goto -12 → target=16-12=4
        0x1B,                 // 19: iload_1
        0xAC,                 // 20: ireturn
    };

    JvmInterpreter interp;
    auto r = interp.run(raw, 3, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // sum = 0+1+2+3+4 = 10
    EXPECT_EQ(r->as_int32(), 10)
        << "Loop must compute sum(0..4) = 10";
}

// ── 006.5: Deopt/state reconstruction — wide iinc with big-endian operands ───
// Verifies the wide form decodes correctly with big-endian u2 operands.
// wide iinc: 0xC4 0x84 u2_local u2_const
TEST(Regression006JvmBigEndian, DeoptStateWideIinc) {
    // wide iinc local=5, const=-1
    // 0xC4 0x84 0x00 0x05 0xFF 0xFF
    std::vector<uint8_t> raw = {0xC4, 0x84, 0x00, 0x05, 0xFF, 0xFF};
    auto d = decode_opcode(raw.data(), raw.size());
    EXPECT_EQ(d.op, JvmOpcode::WideIinc);
    EXPECT_EQ(d.length, 6);
    EXPECT_EQ(d.operand_u32, 5u)
        << "Wide iinc local index must be 5 (big-endian)";
    EXPECT_EQ(d.operand_i32, -1)
        << "Wide iinc const must be -1 (big-endian sign-extended)";
}
