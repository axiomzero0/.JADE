// SPDX-License-Identifier: MIT
// .JADE Compiler — jvm/Opcode.hpp
//
// JVM bytecode opcodes per JVMS §6.5.
// This is the bytecode that .JADE's granit interpreter executes for Java.
//
// We support the FULL opcode set per JVMS §6.5 (not a subset).

#pragma once

#include <cstdint>
#include <string_view>

namespace jade::jvm {

// ─────────────────────────────────────────────────────────────────────────────
// JvmOpcode — JVMS §6.5 opcodes.
//
//   JVM opcodes are 1 byte. The `wide` prefix (0xC4) modifies the next
//   opcode to use 2-byte operands instead of 1-byte. This enum flattens
//   both into a single 16-bit value:
//      1-byte op: 0x00..0xFF (opcode value)
//      wide op:   0x0100 | opcode_value
// ─────────────────────────────────────────────────────────────────────────────
enum class JvmOpcode : uint16_t {
    Nop              = 0x00,
    AconstNull       = 0x01,
    IconstM1         = 0x02,   // iconst_m1
    Iconst0          = 0x03,
    Iconst1          = 0x04,
    Iconst2          = 0x05,
    Iconst3          = 0x06,
    Iconst4          = 0x07,
    Iconst5          = 0x08,
    Lconst0          = 0x09,
    Lconst1          = 0x0A,
    Fconst0          = 0x0B,
    Fconst1          = 0x0C,
    Fconst2          = 0x0D,
    Dconst0          = 0x0E,
    Dconst1          = 0x0F,
    Bipush           = 0x10,    // signed byte
    Sipush           = 0x11,    // signed short
    Ldc              = 0x12,    // u1 index into constant pool
    LdcW             = 0x13,    // u2 index
    Ldc2W            = 0x14,    // u2 index (long/double)

    Iload            = 0x15,
    Lload            = 0x16,
    Fload            = 0x17,
    Dload            = 0x18,
    Aload            = 0x19,
    Iload0           = 0x1A,
    Iload1           = 0x1B,
    Iload2           = 0x1C,
    Iload3           = 0x1D,
    Lload0           = 0x1E,
    Lload1           = 0x1F,
    Lload2           = 0x20,
    Lload3           = 0x21,
    Fload0           = 0x22,
    Fload1           = 0x23,
    Fload2           = 0x24,
    Fload3           = 0x25,
    Dload0           = 0x26,
    Dload1           = 0x27,
    Dload2           = 0x28,
    Dload3           = 0x29,
    Aload0           = 0x2A,
    Aload1           = 0x2B,
    Aload2           = 0x2C,
    Aload3           = 0x2D,

    Iaload           = 0x2E,
    Laload           = 0x2F,
    Faload           = 0x30,
    Daload           = 0x31,
    Aaload           = 0x32,
    Baload           = 0x33,
    Caload           = 0x34,
    Saload           = 0x35,

    Istore           = 0x36,
    Lstore           = 0x37,
    Fstore           = 0x38,
    Dstore           = 0x39,
    Astore           = 0x3A,
    Istore0          = 0x3B,
    Istore1          = 0x3C,
    Istore2          = 0x3D,
    Istore3          = 0x3E,
    Lstore0          = 0x3F,
    Lstore1          = 0x40,
    Lstore2          = 0x41,
    Lstore3          = 0x42,
    Fstore0          = 0x43,
    Fstore1          = 0x44,
    Fstore2          = 0x45,
    Fstore3          = 0x46,
    Dstore0          = 0x47,
    Dstore1          = 0x48,
    Dstore2          = 0x49,
    Dstore3          = 0x4A,
    Astore0          = 0x4B,
    Astore1          = 0x4C,
    Astore2          = 0x4D,
    Astore3          = 0x4E,

    Iastore          = 0x4F,
    Lastore          = 0x50,
    Fastore          = 0x51,
    Dastore          = 0x52,
    Aastore          = 0x53,
    Bastore          = 0x54,
    Castore          = 0x55,
    Sastore          = 0x56,

    Pop              = 0x57,
    Pop2             = 0x58,
    Dup              = 0x59,
    DupX1            = 0x5A,
    DupX2            = 0x5B,
    Dup2             = 0x5C,
    Dup2X1           = 0x5D,
    Dup2X2           = 0x5E,
    Swap             = 0x5F,

    Iadd             = 0x60,
    Ladd             = 0x61,
    Fadd             = 0x62,
    Dadd             = 0x63,
    Isub             = 0x64,
    Lsub             = 0x65,
    Fsub             = 0x66,
    Dsub             = 0x67,
    Imul             = 0x68,
    Lmul             = 0x69,
    Fmul             = 0x6A,
    Dmul             = 0x6B,
    Idiv             = 0x6C,
    Ldiv             = 0x6D,
    Fdiv             = 0x6E,
    Ddiv             = 0x6F,
    Irem             = 0x70,
    Lrem             = 0x71,
    Frem             = 0x72,
    Drem             = 0x73,
    Ineg             = 0x74,
    Lneg             = 0x75,
    Fneg             = 0x76,
    Dneg             = 0x77,
    Ishl             = 0x78,
    Lshl             = 0x79,
    Ishr             = 0x7A,
    Lshr             = 0x7B,
    Iushr            = 0x7C,
    Lushr            = 0x7D,
    Iand             = 0x7E,
    Land             = 0x7F,
    Ior              = 0x80,
    Lor               = 0x81,
    Ixor             = 0x82,
    Lxor             = 0x83,
    Iinc             = 0x84,    // u1 index + s1 const

    // ── Conversions ──
    I2l              = 0x85,
    I2f              = 0x86,
    I2d              = 0x87,
    L2i              = 0x88,
    L2f              = 0x89,
    L2d              = 0x8A,
    F2i              = 0x8B,
    F2l              = 0x8C,
    F2d              = 0x8D,
    D2i              = 0x8E,
    D2l              = 0x8F,
    D2f              = 0x90,
    I2b              = 0x91,
    I2c              = 0x92,
    I2s              = 0x93,

    // ── Comparisons ──
    Lcmp             = 0x94,
    Fcmpl            = 0x95,
    Fcmpg            = 0x96,
    Dcmpl            = 0x97,
    Dcmpg            = 0x98,

    // ── Branches ──
    Ifeq             = 0x99,
    Ifne             = 0x9A,
    Iflt             = 0x9B,
    Ifge             = 0x9C,
    Ifgt             = 0x9D,
    Ifle             = 0x9E,
    IfIcmpeq         = 0x9F,
    IfIcmpne         = 0xA0,
    IfIcmplt         = 0xA1,
    IfIcmpge         = 0xA2,
    IfIcmpgt         = 0xA3,
    IfIcmple         = 0xA4,
    IfAcmpeq         = 0xA5,
    IfAcmpne         = 0xA6,
    Goto             = 0xA7,
    Jsr               = 0xA8,
    Ret               = 0xA9,
    TableSwitch      = 0xAA,
    LookupSwitch     = 0xAB,
    Ireturn          = 0xAC,
    Lreturn          = 0xAD,
    Freturn          = 0xAE,
    Dreturn          = 0xAF,
    Areturn          = 0xB0,
    Return           = 0xB1,

    // ── Fields ──
    Getstatic        = 0xB2,
    Putstatic        = 0xB3,
    Getfield         = 0xB4,
    Putfield         = 0xB5,

    // ── Method invocation ──
    Invokevirtual    = 0xB6,
    Invokespecial    = 0xB7,
    Invokestatic     = 0xB8,
    Invokeinterface  = 0xB9,    // u2 index + u1 count + u1 0
    Invokedynamic    = 0xBA,    // u2 index + u2 0

    // ── Object / array ──
    New              = 0xBB,
    Newarray         = 0xBC,    // u1 atype
    Anewarray        = 0xBD,
    Arraylength      = 0xBE,
    Athrow           = 0xBF,
    Checkcast        = 0xC0,
    Instanceof        = 0xC1,
    Monitorenter     = 0xC2,
    Monitorexit      = 0xC3,

    // ── Wide ──
    Wide             = 0xC4,    // prefix; next opcode uses u2 operands

    // ── Multianewarray ──
    Multianewarray   = 0xC5,    // u2 index + u1 dimensions

    // ── Ifnull/Ifnonnull ──
    Ifnull           = 0xC6,
    Ifnonnull        = 0xC7,

    // ── GotoW / JsrW ──
    GotoW            = 0xC8,
    JsrW             = 0xC9,

    // ── Miscellaneous ──
    Breakpoint       = 0xCA,    // reserved; debugger
    Impdep1          = 0xFE,    // reserved; VM-specific
    Impdep2          = 0xFF,    // reserved; VM-specific

    // ── Wide-form opcodes (0x0100 | base_opcode) ──
    WideIload        = 0x0100 | 0x15,
    WideFload        = 0x0100 | 0x17,
    WideAload        = 0x0100 | 0x19,
    WideLload        = 0x0100 | 0x16,
    WideDload        = 0x0100 | 0x18,
    WideIstore       = 0x0100 | 0x36,
    WideFstore       = 0x0100 | 0x38,
    WideAstore       = 0x0100 | 0x3A,
    WideLstore       = 0x0100 | 0x37,
    WideDstore       = 0x0100 | 0x39,
    WideRet          = 0x0100 | 0xA9,
    WideIinc         = 0x0100 | 0x84,

    // ── Sentinel ──
    Invalid          = 0xFFFF,
};

// Operand format — how many bytes follow the opcode and what they mean.
enum class OperandFormat : uint8_t {
    None,                    // no operand
    U1,                      // 1 unsigned byte
    S1,                      // 1 signed byte (bipush, iinc const)
    U2,                      // 2 unsigned bytes (constant pool index, branch offset)
    S2,                      // 2 signed bytes (sipush, branch offset for goto/etc.)
    S4,                      // 4 signed bytes (goto_w, jsr_w)
    U1U1,                    // 2 bytes: u1 + u1 (iinc: index + const)
    U2U1U1,                  // 4 bytes: u2 + u1 + u1 (invokeinterface: index + count + 0)
    U2U2,                    // 4 bytes: u2 + u2 (invokedynamic: index + 0)
    U2U1,                    // 3 bytes: u2 + u1 (multianewarray: index + dim)
    TableSwitch,             // 0-3 bytes padding + s4 default + s4 low + s4 high + (high-low+1) * s4
    LookupSwitch,            // 0-3 bytes padding + s4 default + s4 npairs + npairs * (s4 match + s4 offset)
    Wide,                    // 0xC4 prefix; next opcode uses u2 operands (handled separately)
};

struct OpcodeInfo {
    std::string_view name;
    OperandFormat operand;
    bool loads_value;        // pushes onto eval stack
    bool stores_value;       // pops from eval stack
    bool is_branch;
    bool is_call;
    bool can_throw;
    bool is_wide;            // true for 0x0100|op wide-form opcodes
};

[[nodiscard]] const OpcodeInfo& opcode_info(JvmOpcode op) noexcept;
[[nodiscard]] std::string_view opcode_name(JvmOpcode op) noexcept;

// Decode one JVM instruction from a byte buffer.
// Returns the opcode and the length of the instruction (opcode + operand).
struct DecodedInstruction {
    JvmOpcode op;
    uint8_t   length;          // total bytes including opcode
    int32_t   operand_i32;    // signed (for bipush, sipush, iinc const)
    uint32_t  operand_u32;    // unsigned (for constant pool indices, local var indices)
    int64_t   operand_i64;    // for ldc2_w (long/double constants)
    double    operand_r8;     // for ldc2_w (double)
    float     operand_r4;    // for ldc (float constant)

    // For tableswitch / lookupswitch
    int32_t   switch_default;             // default offset
    int32_t   switch_low;                 // tableswitch only
    int32_t   switch_high;                // tableswitch only
    int32_t   switch_count;               // both: count of offsets/pairs
    const uint8_t* switch_data;           // pointer to switch offsets table
};

[[nodiscard]] DecodedInstruction decode_opcode(const uint8_t* bytes, std::size_t len);

// atype codes for `newarray` (JVMS §6.5.newarray)
enum class NewArrayType : uint8_t {
    Boolean = 4,
    Char    = 5,
    Float   = 6,
    Double  = 7,
    Byte    = 8,
    Short   = 9,
    Int     = 10,
    Long    = 11,
};
[[nodiscard]] std::string_view newarray_type_name(NewArrayType t) noexcept;

}  // namespace jade::jvm
