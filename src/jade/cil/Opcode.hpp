// SPDX-License-Identifier: MIT
// .JADE Compiler — cil/Opcode.hpp
//
// CIL (Common Intermediate Language) opcodes per ECMA-335.
// This is the bytecode that .JADE's granit interpreter executes for C#.

#pragma once

#include <cstdint>
#include <string_view>

namespace jade::cil {

// ─────────────────────────────────────────────────────────────────────────────
// CilOpcode — ECMA-335 III opcodes (subset for initial milestone).
//
//   - Opcodes are 1 or 2 bytes. The two-byte form has 0xFE as the first byte.
//   - This enum flattens both into a single 16-bit value for simplicity:
//        1-byte op: 0x00..0xFF (opcode value)
//        2-byte op: 0x0100 | second_byte
// ─────────────────────────────────────────────────────────────────────────────
enum class CilOpcode : uint16_t {
    Nop          = 0x00,
    Break        = 0x01,
    LdArg_0      = 0x02,   // ldarg.0
    LdArg_1      = 0x03,
    LdArg_2      = 0x04,
    LdArg_3      = 0x05,
    LdLoc_0      = 0x06,   // ldloc.0
    LdLoc_1      = 0x07,
    LdLoc_2      =0x08,
    LdLoc_3      =0x09,
    StLoc_0      = 0x0A,
    StLoc_1      = 0x0B,
    StLoc_2      = 0x0C,
    StLoc_3      = 0x0D,
    LdArg_S      = 0x0E,   // ldarg.s  uint8
    LdArga_S     = 0x0F,   // ldarga.s uint8
    StArg_S      = 0x10,   // starg.s  uint8
    LdLoc_S      = 0x11,   // ldloc.s  uint8
    LdLoca_S     = 0x12,   // ldloca.s uint8
    StLoc_S      = 0x13,   // stloc.s  uint8
    LdNull       = 0x14,
    LdI4_M1      = 0x15,   // ldc.i4.m1
    LdI4_0       = 0x16,
    LdI4_1       = 0x17,
    LdI4_2       = 0x18,
    LdI4_3       = 0x19,
    LdI4_4       = 0x1A,
    LdI4_5       = 0x1B,
    LdI4_6       = 0x1C,
    LdI4_7       = 0x1D,
    LdI4_8       = 0x1E,
    LdI4_S       = 0x1F,   // ldc.i4.s int8
    LdI4         = 0x20,   // ldc.i4   int32
    LdI8         = 0x21,   // ldc.i8   int64
    LdR4         = 0x22,   // ldc.r4   float32
    LdR8         = 0x23,   // ldc.r8   float64
    LdStr        = 0x72,   // ldstr    token
    Dup          = 0x25,
    Pop          = 0x26,
    Jmp          = 0x27,   // jmp      method
    Call         = 0x28,   // call     method
    Calli        = 0x29,   // calli    callsitedesc
    Ret          = 0x2A,
    Br_S         = 0x2B,   // br.s     int8
    Brfalse_S    = 0x2C,   // brfalse.s int8
    Brtrue_S     = 0x2D,   // brtrue.s  int8
    Beq_S        = 0x2E,   // beq.s     int8
    Bge_S        = 0x2F,
    Bgt_S        = 0x30,
    Ble_S        = 0x31,
    Blt_S        = 0x32,
    Bne_Un_S     = 0x33,
    Bge_Un_S     = 0x34,
    Bgt_Un_S     = 0x35,
    Ble_Un_S     = 0x36,
    Blt_Un_S     = 0x37,
    Br           = 0x38,   // br      int32
    Brfalse      = 0x39,
    Brtrue       = 0x3A,
    Beq          = 0x3B,
    Bge          = 0x3C,
    Bgt          = 0x3D,
    Ble          = 0x3E,
    Blt          = 0x3F,
    Bne_Un       = 0x40,
    Bge_Un       = 0x41,
    Bgt_Un       = 0x42,
    Ble_Un       = 0x43,
    Blt_Un       = 0x44,
    Switch       = 0x45,

    LdInd_I1     = 0x46,   // ldind.i1
    LdInd_U1     = 0x47,
    LdInd_I2     = 0x48,
    LdInd_U2     = 0x49,
    LdInd_I4     = 0x4A,
    LdInd_U4     = 0x4B,
    LdInd_I8     = 0x4C,
    LdInd_I      = 0x4D,
    LdInd_R4     = 0x4E,
    LdInd_R8     = 0x4F,
    LdInd_Ref    = 0x50,
    StInd_Ref    = 0x51,
    StInd_I1     = 0x52,
    StInd_I2     = 0x53,
    StInd_I4     = 0x54,
    StInd_I8     = 0x55,
    StInd_R4     = 0x56,
    StInd_R8     = 0x57,

    Add          = 0x58,
    Sub          = 0x59,
    Mul          = 0x5A,
    Div          = 0x5B,    // signed
    Div_Un       = 0x5C,
    Rem          = 0x5D,
    Rem_Un       = 0x5E,
    And          = 0x5F,
    Or           = 0x60,
    Xor          = 0x61,
    Shl          = 0x62,
    Shr          = 0x63,    // signed
    Shr_Un       = 0x64,
    Neg          = 0x65,
    Not          = 0x66,

    Conv_I1      = 0x67,
    Conv_I2      = 0x68,
    Conv_I4      = 0x69,
    Conv_I8      = 0x6A,
    Conv_R4      = 0x6B,
    Conv_R8      = 0x6C,
    Conv_U4      = 0x6D,
    Conv_U8      = 0x6E,
    Conv_I       = 0xD1,
    Conv_U       = 0xD2,

    CallVirt     = 0x6F,
    LdObj        = 0x71,
    LdStr2       = 0x72,    // alias — kept for completeness; use LdStr above
    NewObj       = 0x73,
    CastClass    = 0x74,
    IsInst       = 0x75,
    Conv_R_Un    = 0x76,
    Unbox        = 0x79,
    Throw        = 0x7A,
    LdFld        = 0x7B,
    LdFlda       = 0x7C,
    StFld        = 0x7D,
    LdsFld       = 0x7E,
    LdsFlda      = 0x7F,
    StsFld       = 0x80,
    StObj        = 0x81,
    Conv_Ovf_I1_Un = 0x82,
    Conv_Ovf_I2_Un = 0x83,
    Conv_Ovf_I4_Un = 0x84,
    Conv_Ovf_I8_Un = 0x85,
    Conv_Ovf_U1_Un = 0x86,
    Conv_Ovf_U2_Un = 0x87,
    Conv_Ovf_U4_Un = 0x88,
    Conv_Ovf_U8_Un = 0x89,
    Conv_Ovf_I1    = 0xB3,
    Conv_Ovf_I2    = 0xB4,
    Conv_Ovf_I4    = 0xB5,
    Conv_Ovf_I8    = 0xB6,
    Conv_Ovf_U1    = 0xB7,
    Conv_Ovf_U2    = 0xB8,
    Conv_Ovf_U4    = 0xB9,
    Conv_Ovf_U8    = 0xBA,

    Box          = 0x8C,
    NewArr       = 0x8D,
    LdLen        = 0x8E,
    LdElema      = 0x8F,    // ldelema
    LdElem_I1    = 0x90,
    LdElem_U1    = 0x91,
    LdElem_I2    = 0x92,
    LdElem_U2    = 0x93,
    LdElem_I4    = 0x94,
    LdElem_U4    = 0x95,
    LdElem_I8    = 0x96,
    LdElem_I     = 0x97,
    LdElem_R4    = 0x98,
    LdElem_R8    = 0x99,
    LdElem_Ref   = 0x9A,
    StElem_I1    = 0x9B,
    StElem_I2    = 0x9C,
    StElem_I4    = 0x9D,
    StElem_I8    = 0x9E,
    StElem_R4    = 0x9F,
    StElem_R8    = 0xA0,
    StElem_Ref   = 0xA2,
    LdElem       = 0xA3,    // ldelem   token
    StElem       = 0xA4,    // stelem   token
    UnboxAny     = 0xA5,

    Conv_Ovf_I   = 0xD3,
    Conv_Ovf_U   = 0xD4,

    // ── 2-byte opcodes (0xFE prefix) ──────────────────────────────────────
    Argiterator  = 0x0100 | 0x00,  // argiterator
    Ceq          = 0x0100 | 0x01,
    Cgt          = 0x0100 | 0x02,
    Cgt_Un       = 0x0100 | 0x03,
    Clt          = 0x0100 | 0x04,
    Clt_Un       = 0x0100 | 0x05,
    LdFtn        = 0x0100 | 0x06,
    LdVirtFtn    = 0x0100 | 0x07,
    LdArg        = 0x0100 | 0x09,   // ldarg    uint16
    LdArga       = 0x0100 | 0x0A,
    StArg        = 0x0100 | 0x0B,
    LdLoc        = 0x0100 | 0x0C,
    LdLoca       = 0x0100 | 0x0D,
    StLoc        = 0x0100 | 0x0E,
    LocalLoc     = 0x0100 | 0x0F,
    EndFilter    = 0x0100 | 0x11,
    Unaligned    = 0x0100 | 0x12,    // prefix
    Volatile     = 0x0100 | 0x13,    // prefix
    Tail         = 0x0100 | 0x14,    // prefix
    InitObj      = 0x0100 | 0x15,
    Constrained  = 0x0100 | 0x16,    // prefix
    CpBlk        = 0x0100 | 0x17,
    InitBlk      = 0x0100 | 0x18,
    No           = 0x0100 | 0x19,    // prefix
    ReThrow      = 0x0100 | 0x1A,
    SizeOf       = 0x0100 | 0x1B,
    RefAnyType   = 0x0100 | 0x1C,
    Readonly     = 0x0100 | 0x1E,    // prefix

    Leave        = 0xDD,    // leave    int32
    Leave_S      = 0xDE,    // leave.s  int8
    EndFinally   = 0xDC,

    // ── Sentinel ──────────────────────────────────────────────────────────
    Invalid      = 0xFFFF,
};

// Operand format — how many bytes follow the opcode, and what they mean.
enum class OperandFormat : uint8_t {
    None,         // no operand
    ShortInlineBrTarget,    // int8   jump offset
    InlineBrTarget,          // int32  jump offset
    ShortInlineI,            // int8
    InlineI,                 // int32
    InlineI8,                // int64
    ShortInlineR,            // float32
    InlineR,                 // float64
    ShortInlineVar,          // uint8  (ldloc.s, ldarg.s)
    InlineVar,               // uint16 (ldloc, ldarg)
    InlineType,              // 4-byte metadata token (TypeRef/TypeDef)
    InlineField,             // 4-byte metadata token (Field)
    InlineMethod,             // 4-byte metadata token (Method)
    InlineString,            // 4-byte metadata token (string literal in #US)
    InlineTok,                // 4-byte generic token
    InlineSig,                // 4-byte StandAloneSig
    InlineSwitch,             // int32 N + N int32 targets
};

struct OpcodeInfo {
    std::string_view name;
    OperandFormat operand;
    bool is_prefix;
    bool loads_value;     // does this push something onto the eval stack?
    bool stores_value;    // does this pop something?
    bool is_branch;
    bool is_call;
    bool can_throw;
};

[[nodiscard]] const OpcodeInfo& opcode_info(CilOpcode op) noexcept;
[[nodiscard]] std::string_view opcode_name(CilOpcode op) noexcept;

// Decode one instruction from a byte buffer.
// Returns the opcode and the length of the instruction (opcode + operand).
// `operand32` is filled with the int32/uint32 operand when present.
struct DecodedInstruction {
    CilOpcode op;
    uint8_t   length;          // total bytes including opcode
    int32_t   operand_i32;     // valid for InlineI / InlineBrTarget / ShortInlineBrTarget sign-extended
    uint32_t  operand_u32;     // valid for tokens, InlineVar, InlineSwitch count, etc.
    int64_t   operand_i64;
    double    operand_r8;
    float     operand_r4;
};

[[nodiscard]] DecodedInstruction decode_opcode(const uint8_t* bytes, std::size_t len);

}  // namespace jade::cil
