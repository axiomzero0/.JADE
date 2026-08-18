// SPDX-License-Identifier: MIT
// .JADE Compiler — cil/Opcode.cpp

#include "jade/cil/Opcode.hpp"

#include <array>
#include <cstring>
#include <cstdint>

namespace jade::cil {

namespace {

// We use a simple approach: a 256-entry table for 1-byte opcodes, plus a
// small switch for the 0xFE-prefixed two-byte opcodes. For the initial
// milestone, we cover the most common subset; unknown opcodes are reported
// as Invalid.

constexpr OpcodeInfo kUnknown = {"<unknown>", OperandFormat::None, false, false, false, false, false, false};

constexpr OpcodeInfo kTable[] = {
    // 0x00 Nop
    {"nop",         OperandFormat::None,        false, false, false, false, false, false},
    // 0x01 Break (debugger breakpoint)
    {"break",        OperandFormat::None,       false, false, false, false, false, false},
    // 0x02 LdArg_0
    {"ldarg.0",     OperandFormat::None,        false, true,  false, false, false, false},
    {"ldarg.1",     OperandFormat::None,        false, true,  false, false, false, false},
    {"ldarg.2",     OperandFormat::None,        false, true,  false, false, false, false},
    {"ldarg.3",     OperandFormat::None,        false, true,  false, false, false, false},
    {"ldloc.0",     OperandFormat::None,        false, true,  false, false, false, false},
    {"ldloc.1",     OperandFormat::None,        false, true,  false, false, false, false},
    {"ldloc.2",     OperandFormat::None,        false, true,  false, false, false, false},
    {"ldloc.3",     OperandFormat::None,        false, true,  false, false, false, false},
    {"stloc.0",     OperandFormat::None,        false, false, true,  false, false, false},
    {"stloc.1",     OperandFormat::None,        false, false, true,  false, false, false},
    {"stloc.2",     OperandFormat::None,        false, false, true,  false, false, false},
    {"stloc.3",     OperandFormat::None,        false, false, true,  false, false, false},
    {"ldarg.s",     OperandFormat::ShortInlineVar, false, true,  false, false, false, false},
    {"ldarga.s",    OperandFormat::ShortInlineVar, false, true,  false, false, false, false},
    {"starg.s",     OperandFormat::ShortInlineVar, false, false, true,  false, false, false},
    {"ldloc.s",     OperandFormat::ShortInlineVar, false, true,  false, false, false, false},
    {"ldloca.s",    OperandFormat::ShortInlineVar, false, true,  false, false, false, false},
    {"stloc.s",     OperandFormat::ShortInlineVar, false, false, true,  false, false, false},
    {"ldnull",      OperandFormat::None,        false, true,  false, false, false, false},
    {"ldc.i4.m1",   OperandFormat::None,        false, true,  false, false, false, false},
    {"ldc.i4.0",    OperandFormat::None,        false, true,  false, false, false, false},
    {"ldc.i4.1",    OperandFormat::None,        false, true,  false, false, false, false},
    {"ldc.i4.2",    OperandFormat::None,        false, true,  false, false, false, false},
    {"ldc.i4.3",    OperandFormat::None,        false, true,  false, false, false, false},
    {"ldc.i4.4",    OperandFormat::None,        false, true,  false, false, false, false},
    {"ldc.i4.5",    OperandFormat::None,        false, true,  false, false, false, false},
    {"ldc.i4.6",    OperandFormat::None,        false, true,  false, false, false, false},
    {"ldc.i4.7",    OperandFormat::None,        false, true,  false, false, false, false},
    {"ldc.i4.8",    OperandFormat::None,        false, true,  false, false, false, false},
    {"ldc.i4.s",    OperandFormat::ShortInlineI, false, true,  false, false, false, false},
    {"ldc.i4",      OperandFormat::InlineI,     false, true,  false, false, false, false},
    {"ldc.i8",      OperandFormat::InlineI8,    false, true,  false, false, false, false},
    {"ldc.r4",      OperandFormat::ShortInlineR, false, true,  false, false, false, false},
    {"ldc.r8",      OperandFormat::InlineR,     false, true,  false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0x24
    {"dup",         OperandFormat::None,        false, true,  false, false, false, false},
    {"pop",         OperandFormat::None,        false, false, true,  false, false, false},
    {"jmp",         OperandFormat::InlineMethod, false, false, false, false, false, false},
    {"call",        OperandFormat::InlineMethod, false, false, false, false, true,  true},
    {"calli",       OperandFormat::InlineSig,    false, false, false, false, true,  true},
    {"ret",         OperandFormat::None,        false, false, false, false, false, false},
    {"br.s",        OperandFormat::ShortInlineBrTarget, false, false, false, true,  false, false},
    {"brfalse.s",   OperandFormat::ShortInlineBrTarget, false, false, true,  true,  false, false},
    {"brtrue.s",    OperandFormat::ShortInlineBrTarget, false, false, true,  true,  false, false},
    {"beq.s",       OperandFormat::ShortInlineBrTarget, false, false, true,  true,  false, false},
    {"bge.s",       OperandFormat::ShortInlineBrTarget, false, false, true,  true,  false, false},
    {"bgt.s",       OperandFormat::ShortInlineBrTarget, false, false, true,  true,  false, false},
    {"ble.s",       OperandFormat::ShortInlineBrTarget, false, false, true,  true,  false, false},
    {"blt.s",       OperandFormat::ShortInlineBrTarget, false, false, true,  true,  false, false},
    {"bne.un.s",    OperandFormat::ShortInlineBrTarget, false, false, true,  true,  false, false},
    {"bge.un.s",    OperandFormat::ShortInlineBrTarget, false, false, true,  true,  false, false},
    {"bgt.un.s",    OperandFormat::ShortInlineBrTarget, false, false, true,  true,  false, false},
    {"ble.un.s",    OperandFormat::ShortInlineBrTarget, false, false, true,  true,  false, false},
    {"blt.un.s",    OperandFormat::ShortInlineBrTarget, false, false, true,  true,  false, false},
    {"br",          OperandFormat::InlineBrTarget, false, false, false, true,  false, false},
    {"brfalse",     OperandFormat::InlineBrTarget, false, false, true,  true,  false, false},
    {"brtrue",      OperandFormat::InlineBrTarget, false, false, true,  true,  false, false},
    {"beq",         OperandFormat::InlineBrTarget, false, false, true,  true,  false, false},
    {"bge",         OperandFormat::InlineBrTarget, false, false, true,  true,  false, false},
    {"bgt",         OperandFormat::InlineBrTarget, false, false, true,  true,  false, false},
    {"ble",         OperandFormat::InlineBrTarget, false, false, true,  true,  false, false},
    {"blt",         OperandFormat::InlineBrTarget, false, false, true,  true,  false, false},
    {"bne.un",      OperandFormat::InlineBrTarget, false, false, true,  true,  false, false},
    {"bge.un",      OperandFormat::InlineBrTarget, false, false, true,  true,  false, false},
    {"bgt.un",      OperandFormat::InlineBrTarget, false, false, true,  true,  false, false},
    {"ble.un",      OperandFormat::InlineBrTarget, false, false, true,  true,  false, false},
    {"blt.un",      OperandFormat::InlineBrTarget, false, false, true,  true,  false, false},
    {"switch",      OperandFormat::InlineSwitch, false, false, false, true,  false, false},
    {"ldind.i1",    OperandFormat::None, false, true,  true,  false, false, true},
    {"ldind.u1",    OperandFormat::None, false, true,  true,  false, false, true},
    {"ldind.i2",    OperandFormat::None, false, true,  true,  false, false, true},
    {"ldind.u2",    OperandFormat::None, false, true,  true,  false, false, true},
    {"ldind.i4",    OperandFormat::None, false, true,  true,  false, false, true},
    {"ldind.u4",    OperandFormat::None, false, true,  true,  false, false, true},
    {"ldind.i8",    OperandFormat::None, false, true,  true,  false, false, true},
    {"ldind.i",     OperandFormat::None, false, true,  true,  false, false, true},
    {"ldind.r4",    OperandFormat::None, false, true,  true,  false, false, true},
    {"ldind.r8",    OperandFormat::None, false, true,  true,  false, false, true},
    {"ldind.ref",   OperandFormat::None, false, true,  true,  false, false, true},
    {"stind.ref",   OperandFormat::None, false, false, true,  false, false, true},
    {"stind.i1",    OperandFormat::None, false, false, true,  false, false, true},
    {"stind.i2",    OperandFormat::None, false, false, true,  false, false, true},
    {"stind.i4",    OperandFormat::None, false, false, true,  false, false, true},
    {"stind.i8",    OperandFormat::None, false, false, true,  false, false, true},
    {"stind.r4",    OperandFormat::None, false, false, true,  false, false, true},
    {"stind.r8",    OperandFormat::None, false, false, true,  false, false, true},
    {"add",         OperandFormat::None, false, true,  true,  false, false, false},
    {"sub",         OperandFormat::None, false, true,  true,  false, false, false},
    {"mul",         OperandFormat::None, false, true,  true,  false, false, false},
    {"div",         OperandFormat::None, false, true,  true,  false, false, true},
    {"div.un",      OperandFormat::None, false, true,  true,  false, false, true},
    {"rem",         OperandFormat::None, false, true,  true,  false, false, true},
    {"rem.un",      OperandFormat::None, false, true,  true,  false, false, true},
    {"and",         OperandFormat::None, false, true,  true,  false, false, false},
    {"or",          OperandFormat::None, false, true,  true,  false, false, false},
    {"xor",         OperandFormat::None, false, true,  true,  false, false, false},
    {"shl",         OperandFormat::None, false, true,  true,  false, false, false},
    {"shr",         OperandFormat::None, false, true,  true,  false, false, false},
    {"shr.un",      OperandFormat::None, false, true,  true,  false, false, false},
    {"neg",         OperandFormat::None, false, true,  true,  false, false, false},
    {"not",         OperandFormat::None, false, true,  true,  false, false, false},
    {"conv.i1",     OperandFormat::None, false, true,  true,  false, false, false},
    {"conv.i2",     OperandFormat::None, false, true,  true,  false, false, false},
    {"conv.i4",     OperandFormat::None, false, true,  true,  false, false, false},
    {"conv.i8",     OperandFormat::None, false, true,  true,  false, false, false},
    {"conv.r4",     OperandFormat::None, false, true,  true,  false, false, false},
    {"conv.r8",     OperandFormat::None, false, true,  true,  false, false, false},
    {"conv.u4",     OperandFormat::None, false, true,  true,  false, false, false},
    {"conv.u8",     OperandFormat::None, false, true,  true,  false, false, false},
    {"callvirt",    OperandFormat::InlineMethod, false, false, false, false, true,  true},   // 0x6F
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},             // 0x70 = cpblk
    {"ldobj",       OperandFormat::InlineType,  false, true,  true,  false, false, true},        // 0x71
    {"ldstr",       OperandFormat::InlineString, false, true,  false, false, false, false},     // 0x72
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},             // 0x73 = ldftn (placeholder)
    {"newobj",      OperandFormat::InlineMethod, false, false, false, false, true,  true},   // 0x74
    {"castclass",   OperandFormat::InlineType,  false, true,  true,  false, false, true},
    {"isinst",      OperandFormat::InlineType,  false, true,  true,  false, false, true},
    {"conv.r.un",   OperandFormat::None, false, true,  true,  false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0x77
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0x78
    {"unbox",       OperandFormat::InlineType,  false, true,  true,  false, false, true},
    {"throw",       OperandFormat::None,        false, false, true,  false, false, true},
    {"ldfld",       OperandFormat::InlineField, false, true,  true,  false, false, true},
    {"ldflda",      OperandFormat::InlineField, false, true,  true,  false, false, true},
    {"stfld",       OperandFormat::InlineField, false, false, true,  false, false, true},
    {"ldsfld",      OperandFormat::InlineField, false, true,  false, false, false, true},
    {"ldsflda",     OperandFormat::InlineField, false, true,  false, false, false, true},
    {"stsfld",      OperandFormat::InlineField, false, false, true,  false, false, true},
    {"stobj",       OperandFormat::InlineType,  false, false, true,  false, false, true},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {"box",         OperandFormat::InlineType,  false, true,  true,  false, false, true},
    {"newarr",      OperandFormat::InlineType,  false, true,  true,  false, false, true},
    {"ldlen",       OperandFormat::None,        false, true,  true,  false, false, true},
    {"ldelema",     OperandFormat::InlineType,  false, true,  true,  false, false, true},
    {"ldelem.i1",   OperandFormat::None,        false, true,  true,  false, false, true},
    {"ldelem.u1",   OperandFormat::None,        false, true,  true,  false, false, true},
    {"ldelem.i2",   OperandFormat::None,        false, true,  true,  false, false, true},
    {"ldelem.u2",   OperandFormat::None,        false, true,  true,  false, false, true},
    {"ldelem.i4",   OperandFormat::None,        false, true,  true,  false, false, true},
    {"ldelem.u4",   OperandFormat::None,        false, true,  true,  false, false, true},
    {"ldelem.i8",   OperandFormat::None,        false, true,  true,  false, false, true},
    {"ldelem.i",    OperandFormat::None,        false, true,  true,  false, false, true},
    {"ldelem.r4",   OperandFormat::None,        false, true,  true,  false, false, true},
    {"ldelem.r8",   OperandFormat::None,        false, true,  true,  false, false, true},
    {"ldelem.ref",  OperandFormat::None,        false, true,  true,  false, false, true},
    {"stelem.i1",   OperandFormat::None,        false, false, true,  false, false, true},
    {"stelem.i2",   OperandFormat::None,        false, false, true,  false, false, true},
    {"stelem.i4",   OperandFormat::None,        false, false, true,  false, false, true},
    {"stelem.i8",   OperandFormat::None,        false, false, true,  false, false, true},
    {"stelem.r4",   OperandFormat::None,        false, false, true,  false, false, true},
    {"stelem.r8",   OperandFormat::None,        false, false, true,  false, false, true},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xA1
    {"stelem.ref",  OperandFormat::None,        false, false, true,  false, false, true},
    {"ldelem",      OperandFormat::InlineType,  false, true,  true,  false, false, true},
    {"stelem",      OperandFormat::InlineType,  false, false, true,  false, false, true},
    {"unbox.any",   OperandFormat::InlineType,  false, true,  true,  false, false, true},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xA6
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xCB
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xD1
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xD2
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {"endfinally",  OperandFormat::None,        false, false, false, false, false, false},
    {"leave",       OperandFormat::InlineBrTarget, false, false, false, true, false, false},
    {"leave.s",     OperandFormat::ShortInlineBrTarget, false, false, false, true, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xDF
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xEF
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xF5
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xF6
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xF7
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xF8
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xF9
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xFA
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xFB
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xFC
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xFD
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xFE — handled separately
    {kUnknown.name, OperandFormat::None, false, false, false, false, false, false},  // 0xFF
};

static_assert(std::size(kTable) == 256, "kTable must cover all 256 1-byte opcode values");

// 0xFE-prefixed two-byte opcodes (small table, indexed by second byte).
struct TwoByteEntry {
    CilOpcode op;
    OpcodeInfo info;
};

constexpr TwoByteEntry kTwoByte[] = {
    {CilOpcode::Ceq,         {"ceq",        OperandFormat::None, false, true, true, false, false, false}},
    {CilOpcode::Cgt,         {"cgt",        OperandFormat::None, false, true, true, false, false, false}},
    {CilOpcode::Cgt_Un,      {"cgt.un",     OperandFormat::None, false, true, true, false, false, false}},
    {CilOpcode::Clt,         {"clt",        OperandFormat::None, false, true, true, false, false, false}},
    {CilOpcode::Clt_Un,      {"clt.un",     OperandFormat::None, false, true, true, false, false, false}},
    {CilOpcode::LdArg,       {"ldarg",      OperandFormat::InlineVar, false, true, false, false, false, false}},
    {CilOpcode::LdArga,      {"ldarga",     OperandFormat::InlineVar, false, true, false, false, false, false}},
    {CilOpcode::StArg,       {"starg",      OperandFormat::InlineVar, false, false, true, false, false, false}},
    {CilOpcode::LdLoc,       {"ldloc",      OperandFormat::InlineVar, false, true, false, false, false, false}},
    {CilOpcode::LdLoca,      {"ldloca",     OperandFormat::InlineVar, false, true, false, false, false, false}},
    {CilOpcode::StLoc,       {"stloc",      OperandFormat::InlineVar, false, false, true, false, false, false}},
    {CilOpcode::EndFilter,   {"endfilter",  OperandFormat::None, false, false, false, false, false, false}},
    {CilOpcode::Unaligned,   {"unaligned.", OperandFormat::ShortInlineI, true, false, false, false, false, false}},
    {CilOpcode::Volatile,    {"volatile.",  OperandFormat::None, true, false, false, false, false, false}},
    {CilOpcode::Tail,        {"tail.",       OperandFormat::None, true, false, false, false, false, false}},
    {CilOpcode::InitObj,     {"initobj",    OperandFormat::InlineType, false, false, true, false, false, false}},
    {CilOpcode::Constrained, {"constrained.", OperandFormat::InlineType, true, false, false, false, false, false}},
    {CilOpcode::ReThrow,     {"rethrow",    OperandFormat::None, false, false, false, false, false, true}},
    {CilOpcode::SizeOf,      {"sizeof",     OperandFormat::InlineType, false, true, false, false, false, false}},
    {CilOpcode::Readonly,    {"readonly.",   OperandFormat::None, true, false, false, false, false, false}},
};

}  // namespace

const OpcodeInfo& opcode_info(CilOpcode op) noexcept {
    const auto v = static_cast<uint32_t>(op);
    if (v <= 0xFF) {
        return kTable[v];
    }
    // Two-byte opcode.
    const auto second = static_cast<uint8_t>(v & 0xFF);
    for (const auto& e : kTwoByte) {
        if (static_cast<uint32_t>(e.op) == v) return e.info;
    }
    return kUnknown;
}

std::string_view opcode_name(CilOpcode op) noexcept {
    return opcode_info(op).name;
}

// ─────────────────────────────────────────────────────────────────────────────
// decode_opcode — parse one CIL instruction from a byte stream.
// ─────────────────────────────────────────────────────────────────────────────
DecodedInstruction decode_opcode(const uint8_t* bytes, std::size_t len) {
    DecodedInstruction d{};
    if (len == 0) {
        d.op = CilOpcode::Invalid;
        d.length = 0;
        return d;
    }

    const uint8_t first = bytes[0];
    if (first != 0xFE) {
        // Check if this is a known 1-byte opcode.
        const OpcodeInfo& info = kTable[first];
        if (info.name == kUnknown.name) {
            // Unknown opcode — return Invalid.
            d.op = CilOpcode::Invalid;
            d.length = 1;
            return d;
        }
        d.op = static_cast<CilOpcode>(first);
        d.length = 1;
    } else {
        if (len < 2) {
            d.op = CilOpcode::Invalid;
            d.length = 1;
            return d;
        }
        const uint8_t second = bytes[1];
        // Search the two-byte table.
        bool found = false;
        for (const auto& e : kTwoByte) {
            if (static_cast<uint8_t>(static_cast<uint32_t>(e.op) & 0xFF) == second) {
                d.op = e.op;
                found = true;
                break;
            }
        }
        if (!found) {
            d.op = CilOpcode::Invalid;
            d.length = 2;
            return d;
        }
        d.length = 2;
    }

    const OpcodeInfo& info = opcode_info(d.op);
    switch (info.operand) {
        case OperandFormat::None:
            break;
        case OperandFormat::ShortInlineBrTarget:
        case OperandFormat::ShortInlineI:
        case OperandFormat::ShortInlineVar:
            if (len < d.length + 1) { d.op = CilOpcode::Invalid; return d; }
            d.operand_i32 = static_cast<int8_t>(bytes[d.length]);  // sign-extend
            d.operand_u32 = static_cast<uint8_t>(bytes[d.length]);
            d.length += 1;
            break;
        case OperandFormat::InlineVar:
            // Per ECMA-335: InlineVar is uint16 (2 bytes), used by ldloc/ldarg/stloc/starg.
            if (len < d.length + 2) { d.op = CilOpcode::Invalid; return d; }
            {
                uint16_t v;
                std::memcpy(&v, bytes + d.length, 2);
                d.operand_u32 = v;
                d.operand_i32 = static_cast<int32_t>(v);
            }
            d.length += 2;
            break;
        case OperandFormat::InlineBrTarget:
        case OperandFormat::InlineI:
        case OperandFormat::InlineType:
        case OperandFormat::InlineField:
        case OperandFormat::InlineMethod:
        case OperandFormat::InlineString:
        case OperandFormat::InlineTok:
        case OperandFormat::InlineSig:
            if (len < d.length + 4) { d.op = CilOpcode::Invalid; return d; }
            int32_t v;
            std::memcpy(&v, bytes + d.length, 4);
            d.operand_i32 = v;
            d.operand_u32 = static_cast<uint32_t>(v);
            d.length += 4;
            break;
        case OperandFormat::InlineI8:
            if (len < d.length + 8) { d.op = CilOpcode::Invalid; return d; }
            int64_t v8;
            std::memcpy(&v8, bytes + d.length, 8);
            d.operand_i64 = v8;
            d.length += 8;
            break;
        case OperandFormat::ShortInlineR:
            if (len < d.length + 4) { d.op = CilOpcode::Invalid; return d; }
            {
                float f;
                std::memcpy(&f, bytes + d.length, 4);
                d.operand_r4 = f;
            }
            d.length += 4;
            break;
        case OperandFormat::InlineR:
            if (len < d.length + 8) { d.op = CilOpcode::Invalid; return d; }
            {
                double dd;
                std::memcpy(&dd, bytes + d.length, 8);
                d.operand_r8 = dd;
            }
            d.length += 8;
            break;
        case OperandFormat::InlineSwitch:
            // int32 N + N int32 targets
            if (len < d.length + 4) { d.op = CilOpcode::Invalid; return d; }
            int32_t n;
            std::memcpy(&n, bytes + d.length, 4);
            d.operand_u32 = static_cast<uint32_t>(n);
            d.length += 4 + static_cast<int>(n) * 4;
            break;
    }
    return d;
}

}  // namespace jade::cil
