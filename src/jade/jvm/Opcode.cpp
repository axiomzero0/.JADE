// SPDX-License-Identifier: MIT
// .JADE Compiler — jvm/Opcode.cpp

#include "jade/jvm/Opcode.hpp"

#include <array>
#include <cstring>
#include <cstdint>

namespace jade::jvm {

namespace {

constexpr OpcodeInfo kUnknown = {"<unknown>", OperandFormat::None, false, false, false, false, false, false};

// 256-entry table for 1-byte opcodes.
// Order matches JVMS §6.5 exactly.
constexpr OpcodeInfo kTable[256] = {
    /* 0x00 */ {"nop",            OperandFormat::None,     false, false, false, false, false, false},
    /* 0x01 */ {"aconst_null",    OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x02 */ {"iconst_m1",      OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x03 */ {"iconst_0",       OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x04 */ {"iconst_1",       OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x05 */ {"iconst_2",       OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x06 */ {"iconst_3",       OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x07 */ {"iconst_4",       OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x08 */ {"iconst_5",       OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x09 */ {"lconst_0",       OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x0A */ {"lconst_1",       OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x0B */ {"fconst_0",       OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x0C */ {"fconst_1",       OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x0D */ {"fconst_2",       OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x0E */ {"dconst_0",       OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x0F */ {"dconst_1",       OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x10 */ {"bipush",         OperandFormat::S1,        true,  false, false, false, false, false},
    /* 0x11 */ {"sipush",         OperandFormat::S2,        true,  false, false, false, false, false},
    /* 0x12 */ {"ldc",            OperandFormat::U1,        true,  false, false, false, false, false},
    /* 0x13 */ {"ldc_w",          OperandFormat::U2,        true,  false, false, false, false, false},
    /* 0x14 */ {"ldc2_w",         OperandFormat::U2,        true,  false, false, false, false, false},
    /* 0x15 */ {"iload",          OperandFormat::U1,        true,  false, false, false, false, false},
    /* 0x16 */ {"lload",          OperandFormat::U1,        true,  false, false, false, false, false},
    /* 0x17 */ {"fload",          OperandFormat::U1,        true,  false, false, false, false, false},
    /* 0x18 */ {"dload",          OperandFormat::U1,        true,  false, false, false, false, false},
    /* 0x19 */ {"aload",          OperandFormat::U1,        true,  false, false, false, false, false},
    /* 0x1A */ {"iload_0",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x1B */ {"iload_1",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x1C */ {"iload_2",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x1D */ {"iload_3",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x1E */ {"lload_0",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x1F */ {"lload_1",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x20 */ {"lload_2",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x21 */ {"lload_3",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x22 */ {"fload_0",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x23 */ {"fload_1",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x24 */ {"fload_2",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x25 */ {"fload_3",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x26 */ {"dload_0",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x27 */ {"dload_1",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x28 */ {"dload_2",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x29 */ {"dload_3",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x2A */ {"aload_0",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x2B */ {"aload_1",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x2C */ {"aload_2",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x2D */ {"aload_3",        OperandFormat::None,     true,  false, false, false, false, false},
    /* 0x2E */ {"iaload",         OperandFormat::None,     true,  true,  false, false, true, false},
    /* 0x2F */ {"laload",         OperandFormat::None,     true,  true,  false, false, true, false},
    /* 0x30 */ {"faload",         OperandFormat::None,     true,  true,  false, false, true, false},
    /* 0x31 */ {"daload",         OperandFormat::None,     true,  true,  false, false, true, false},
    /* 0x32 */ {"aaload",         OperandFormat::None,     true,  true,  false, false, true, false},
    /* 0x33 */ {"baload",         OperandFormat::None,     true,  true,  false, false, true, false},
    /* 0x34 */ {"caload",         OperandFormat::None,     true,  true,  false, false, true, false},
    /* 0x35 */ {"saload",         OperandFormat::None,     true,  true,  false, false, true, false},
    /* 0x36 */ {"istore",         OperandFormat::U1,        false, true,  false, false, false, false},
    /* 0x37 */ {"lstore",         OperandFormat::U1,        false, true,  false, false, false, false},
    /* 0x38 */ {"fstore",         OperandFormat::U1,        false, true,  false, false, false, false},
    /* 0x39 */ {"dstore",         OperandFormat::U1,        false, true,  false, false, false, false},
    /* 0x3A */ {"astore",         OperandFormat::U1,        false, true,  false, false, false, false},
    /* 0x3B */ {"istore_0",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x3C */ {"istore_1",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x3D */ {"istore_2",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x3E */ {"istore_3",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x3F */ {"lstore_0",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x40 */ {"lstore_1",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x41 */ {"lstore_2",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x42 */ {"lstore_3",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x43 */ {"fstore_0",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x44 */ {"fstore_1",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x45 */ {"fstore_2",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x46 */ {"fstore_3",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x47 */ {"dstore_0",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x48 */ {"dstore_1",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x49 */ {"dstore_2",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x4A */ {"dstore_3",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x4B */ {"astore_0",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x4C */ {"astore_1",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x4D */ {"astore_2",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x4E */ {"astore_3",       OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x4F */ {"iastore",        OperandFormat::None,     false, true,  false, false, true, false},
    /* 0x50 */ {"lastore",        OperandFormat::None,     false, true,  false, false, true, false},
    /* 0x51 */ {"fastore",        OperandFormat::None,     false, true,  false, false, true, false},
    /* 0x52 */ {"dastore",        OperandFormat::None,     false, true,  false, false, true, false},
    /* 0x53 */ {"aastore",        OperandFormat::None,     false, true,  false, false, true, false},
    /* 0x54 */ {"bastore",        OperandFormat::None,     false, true,  false, false, true, false},
    /* 0x55 */ {"castore",        OperandFormat::None,     false, true,  false, false, true, false},
    /* 0x56 */ {"sastore",        OperandFormat::None,     false, true,  false, false, true, false},
    /* 0x57 */ {"pop",            OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x58 */ {"pop2",           OperandFormat::None,     false, true,  false, false, false, false},
    /* 0x59 */ {"dup",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x5A */ {"dup_x1",         OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x5B */ {"dup_x2",         OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x5C */ {"dup2",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x5D */ {"dup2_x1",        OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x5E */ {"dup2_x2",        OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x5F */ {"swap",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x60 */ {"iadd",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x61 */ {"ladd",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x62 */ {"fadd",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x63 */ {"dadd",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x64 */ {"isub",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x65 */ {"lsub",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x66 */ {"fsub",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x67 */ {"dsub",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x68 */ {"imul",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x69 */ {"lmul",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x6A */ {"fmul",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x6B */ {"dmul",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x6C */ {"idiv",           OperandFormat::None,     true,  true,  false, false, true, false},
    /* 0x6D */ {"ldiv",           OperandFormat::None,     true,  true,  false, false, true, false},
    /* 0x6E */ {"fdiv",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x6F */ {"ddiv",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x70 */ {"irem",           OperandFormat::None,     true,  true,  false, false, true, false},
    /* 0x71 */ {"lrem",           OperandFormat::None,     true,  true,  false, false, true, false},
    /* 0x72 */ {"frem",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x73 */ {"drem",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x74 */ {"ineg",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x75 */ {"lneg",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x76 */ {"fneg",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x77 */ {"dneg",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x78 */ {"ishl",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x79 */ {"lshl",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x7A */ {"ishr",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x7B */ {"lshr",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x7C */ {"iushr",          OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x7D */ {"lushr",          OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x7E */ {"iand",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x7F */ {"land",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x80 */ {"ior",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x81 */ {"lor",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x82 */ {"ixor",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x83 */ {"lxor",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x84 */ {"iinc",           OperandFormat::U1U1,     false, false, false, false, false, false},
    /* 0x85 */ {"i2l",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x86 */ {"i2f",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x87 */ {"i2d",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x88 */ {"l2i",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x89 */ {"l2f",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x8A */ {"l2d",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x8B */ {"f2i",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x8C */ {"f2l",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x8D */ {"f2d",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x8E */ {"d2i",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x8F */ {"d2l",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x90 */ {"d2f",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x91 */ {"i2b",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x92 */ {"i2c",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x93 */ {"i2s",            OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x94 */ {"lcmp",           OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x95 */ {"fcmpl",          OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x96 */ {"fcmpg",          OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x97 */ {"dcmpl",          OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x98 */ {"dcmpg",          OperandFormat::None,     true,  true,  false, false, false, false},
    /* 0x99 */ {"ifeq",           OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0x9A */ {"ifne",           OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0x9B */ {"iflt",           OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0x9C */ {"ifge",           OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0x9D */ {"ifgt",           OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0x9E */ {"ifle",           OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0x9F */ {"if_icmpeq",      OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0xA0 */ {"if_icmpne",      OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0xA1 */ {"if_icmplt",      OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0xA2 */ {"if_icmpge",      OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0xA3 */ {"if_icmpgt",      OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0xA4 */ {"if_icmple",      OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0xA5 */ {"if_acmpeq",      OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0xA6 */ {"if_acmpne",      OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0xA7 */ {"goto",           OperandFormat::S2,        false, false, true,  false, false, false},
    /* 0xA8 */ {"jsr",            OperandFormat::S2,        true,  false, true,  false, false, false},
    /* 0xA9 */ {"ret",            OperandFormat::U1,        false, false, true,  false, false, false},
    /* 0xAA */ {"tableswitch",    OperandFormat::TableSwitch, false, false, true, false, false, false},
    /* 0xAB */ {"lookupswitch",   OperandFormat::LookupSwitch, false, false, true, false, false, false},
    /* 0xAC */ {"ireturn",        OperandFormat::None,     false, true,  false, false, false, false},
    /* 0xAD */ {"lreturn",        OperandFormat::None,     false, true,  false, false, false, false},
    /* 0xAE */ {"freturn",        OperandFormat::None,     false, true,  false, false, false, false},
    /* 0xAF */ {"dreturn",        OperandFormat::None,     false, true,  false, false, false, false},
    /* 0xB0 */ {"areturn",        OperandFormat::None,     false, true,  false, false, false, false},
    /* 0xB1 */ {"return",         OperandFormat::None,     false, false, false, false, false, false},
    /* 0xB2 */ {"getstatic",      OperandFormat::U2,        true,  false, false, false, true, false},
    /* 0xB3 */ {"putstatic",      OperandFormat::U2,        false, true,  false, false, true, false},
    /* 0xB4 */ {"getfield",       OperandFormat::U2,        true,  true,  false, false, true, false},
    /* 0xB5 */ {"putfield",       OperandFormat::U2,        false, true,  false, false, true, false},
    /* 0xB6 */ {"invokevirtual",  OperandFormat::U2,        false, true,  false, true,  true, false},
    /* 0xB7 */ {"invokespecial",  OperandFormat::U2,        false, true,  false, true,  true, false},
    /* 0xB8 */ {"invokestatic",   OperandFormat::U2,        false, true,  false, true,  true, false},
    /* 0xB9 */ {"invokeinterface", OperandFormat::U2U1U1,   false, true,  false, true,  true, false},
    /* 0xBA */ {"invokedynamic",  OperandFormat::U2U2,      false, true,  false, true,  true, false},
    /* 0xBB */ {"new",            OperandFormat::U2,        true,  false, false, false, true, false},
    /* 0xBC */ {"newarray",       OperandFormat::U1,        true,  true,  false, false, true, false},
    /* 0xBD */ {"anewarray",      OperandFormat::U2,        true,  true,  false, false, true, false},
    /* 0xBE */ {"arraylength",    OperandFormat::None,     true,  true,  false, false, true, false},
    /* 0xBF */ {"athrow",         OperandFormat::None,     false, true,  false, false, true, false},
    /* 0xC0 */ {"checkcast",      OperandFormat::U2,        true,  true,  false, false, true, false},
    /* 0xC1 */ {"instanceof",     OperandFormat::U2,        true,  true,  false, false, true, false},
    /* 0xC2 */ {"monitorenter",   OperandFormat::None,     false, true,  false, false, true, false},
    /* 0xC3 */ {"monitorexit",    OperandFormat::None,     false, true,  false, false, true, false},
    /* 0xC4 */ {"wide",           OperandFormat::Wide,      false, false, false, false, false, false},
    /* 0xC5 */ {"multianewarray", OperandFormat::U2U1,      true,  true,  false, false, true, false},
    /* 0xC6 */ {"ifnull",          OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0xC7 */ {"ifnonnull",      OperandFormat::S2,        false, true,  true,  false, false, false},
    /* 0xC8 */ {"goto_w",         OperandFormat::S4,        false, false, true,  false, false, false},
    /* 0xC9 */ {"jsr_w",          OperandFormat::S4,        true,  false, true,  false, false, false},
    /* 0xCA */ {"breakpoint",     OperandFormat::None,     false, false, false, false, false, false},
    /* 0xCB-0xFD */ kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown,
                   kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown,
                   kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown,
                   kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown,
                   kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown,
                   kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown,
    /* 0xFE */ {"impdep1",        OperandFormat::None,     false, false, false, false, false, false},
    /* 0xFF */ {"impdep2",        OperandFormat::None,     false, false, false, false, false, false},
};

// Wide-form opcodes (0x0100 | base_opcode). Small table, searched linearly.
struct WideEntry {
    JvmOpcode wide_op;
    JvmOpcode base_op;
};
constexpr WideEntry kWide[] = {
    {JvmOpcode::WideIload,   JvmOpcode::Iload},
    {JvmOpcode::WideFload,   JvmOpcode::Fload},
    {JvmOpcode::WideAload,   JvmOpcode::Aload},
    {JvmOpcode::WideLload,   JvmOpcode::Lload},
    {JvmOpcode::WideDload,   JvmOpcode::Dload},
    {JvmOpcode::WideIstore,  JvmOpcode::Istore},
    {JvmOpcode::WideFstore,  JvmOpcode::Fstore},
    {JvmOpcode::WideAstore,  JvmOpcode::Astore},
    {JvmOpcode::WideLstore,  JvmOpcode::Lstore},
    {JvmOpcode::WideDstore,  JvmOpcode::Dstore},
    {JvmOpcode::WideRet,     JvmOpcode::Ret},
    {JvmOpcode::WideIinc,    JvmOpcode::Iinc},
};

}  // namespace

const OpcodeInfo& opcode_info(JvmOpcode op) noexcept {
    const auto v = static_cast<uint32_t>(op);
    if (v <= 0xFF) {
        return kTable[v];
    }
    // Wide-form opcode.
    for (const auto& e : kWide) {
        if (e.wide_op == op) {
            const OpcodeInfo& base = kTable[static_cast<uint8_t>(e.base_op)];
            // Construct a wide-form info — same as base but with is_wide = true
            // and a different operand format.
            static OpcodeInfo wide_info;
            wide_info = base;
            wide_info.is_wide = true;
            // Wide-form uses u2 for the local index, plus u2 for iinc const
            if (e.base_op == JvmOpcode::Iinc) {
                wide_info.operand = OperandFormat::U1U1;  // We'll handle specially: actually u2+u2
            } else {
                wide_info.operand = OperandFormat::U2;
            }
            return wide_info;
        }
    }
    return kUnknown;
}

std::string_view opcode_name(JvmOpcode op) noexcept {
    return opcode_info(op).name;
}

std::string_view newarray_type_name(NewArrayType t) noexcept {
    switch (t) {
        case NewArrayType::Boolean: return "boolean[]";
        case NewArrayType::Char:    return "char[]";
        case NewArrayType::Float:   return "float[]";
        case NewArrayType::Double:  return "double[]";
        case NewArrayType::Byte:    return "byte[]";
        case NewArrayType::Short:   return "short[]";
        case NewArrayType::Int:    return "int[]";
        case NewArrayType::Long:    return "long[]";
    }
    return "unknown[]";
}

// ─────────────────────────────────────────────────────────────────────────────
// decode_opcode — parse one JVM instruction from a byte stream.
// ─────────────────────────────────────────────────────────────────────────────
DecodedInstruction decode_opcode(const uint8_t* bytes, std::size_t len) {
    DecodedInstruction d{};
    if (len == 0) {
        d.op = JvmOpcode::Invalid;
        d.length = 0;
        return d;
    }

    const uint8_t first = bytes[0];
    d.length = 1;

    // Handle `wide` prefix specially.
    if (first == 0xC4) {
        if (len < 2) {
            d.op = JvmOpcode::Invalid;
            return d;
        }
        const uint8_t next = bytes[1];
        d.length = 2;
        // Find the wide-form opcode.
        bool found = false;
        for (const auto& e : kWide) {
            if (static_cast<uint8_t>(e.base_op) == next) {
                d.op = e.wide_op;
                found = true;
                break;
            }
        }
        if (!found) {
            d.op = JvmOpcode::Invalid;
            return d;
        }

        // Wide-form operands:
        // - For most: u2 (local index)
        // - For wide iinc: u2 + u2 (local index + const)
        if (d.op == JvmOpcode::WideIinc) {
            if (len < d.length + 4) { d.op = JvmOpcode::Invalid; return d; }
            uint16_t idx, const_val;
            std::memcpy(&idx, bytes + d.length, 2);
            std::memcpy(&const_val, bytes + d.length + 2, 2);
            d.operand_u32 = idx;
            d.operand_i32 = static_cast<int16_t>(const_val);   // sign-extended
            d.length += 4;
        } else {
            if (len < d.length + 2) { d.op = JvmOpcode::Invalid; return d; }
            uint16_t idx;
            std::memcpy(&idx, bytes + d.length, 2);
            d.operand_u32 = idx;
            d.length += 2;
        }
        return d;
    }

    if (first >= 0xCB && first != 0xFE && first != 0xFF) {
        // Reserved/unused opcode.
        d.op = JvmOpcode::Invalid;
        return d;
    }

    d.op = static_cast<JvmOpcode>(first);
    const OpcodeInfo& info = kTable[first];
    if (info.name == kUnknown.name) {
        d.op = JvmOpcode::Invalid;
        return d;
    }

    switch (info.operand) {
        case OperandFormat::None:
            break;
        case OperandFormat::U1:
            if (len < d.length + 1) { d.op = JvmOpcode::Invalid; return d; }
            d.operand_u32 = bytes[d.length];
            d.operand_i32 = bytes[d.length];
            d.length += 1;
            break;
        case OperandFormat::S1:
            if (len < d.length + 1) { d.op = JvmOpcode::Invalid; return d; }
            d.operand_i32 = static_cast<int8_t>(bytes[d.length]);
            d.operand_u32 = static_cast<uint8_t>(bytes[d.length]);
            d.length += 1;
            break;
        case OperandFormat::U2:
            if (len < d.length + 2) { d.op = JvmOpcode::Invalid; return d; }
            {
                uint16_t v;
                std::memcpy(&v, bytes + d.length, 2);
                d.operand_u32 = v;
                d.operand_i32 = static_cast<int32_t>(v);
            }
            d.length += 2;
            break;
        case OperandFormat::S2:
            if (len < d.length + 2) { d.op = JvmOpcode::Invalid; return d; }
            {
                int16_t v;
                std::memcpy(&v, bytes + d.length, 2);
                d.operand_i32 = v;
                d.operand_u32 = static_cast<uint16_t>(v);
            }
            d.length += 2;
            break;
        case OperandFormat::S4:
            if (len < d.length + 4) { d.op = JvmOpcode::Invalid; return d; }
            {
                int32_t v;
                std::memcpy(&v, bytes + d.length, 4);
                d.operand_i32 = v;
                d.operand_u32 = static_cast<uint32_t>(v);
            }
            d.length += 4;
            break;
        case OperandFormat::U1U1: {
            // iinc: u1 local index + s1 const
            if (len < d.length + 2) { d.op = JvmOpcode::Invalid; return d; }
            uint8_t idx = bytes[d.length];
            int8_t  const_val = static_cast<int8_t>(bytes[d.length + 1]);
            d.operand_u32 = idx;
            d.operand_i32 = const_val;
            d.length += 2;
            break;
        }
        case OperandFormat::U2U1U1: {
            // invokeinterface: u2 index + u1 count + u1 zero
            if (len < d.length + 4) { d.op = JvmOpcode::Invalid; return d; }
            uint16_t idx;
            std::memcpy(&idx, bytes + d.length, 2);
            d.operand_u32 = idx;
            d.length += 4;
            break;
        }
        case OperandFormat::U2U2: {
            // invokedynamic: u2 index + u2 zero
            if (len < d.length + 4) { d.op = JvmOpcode::Invalid; return d; }
            uint16_t idx;
            std::memcpy(&idx, bytes + d.length, 2);
            d.operand_u32 = idx;
            d.length += 4;
            break;
        }
        case OperandFormat::U2U1: {
            // multianewarray: u2 index + u1 dim
            if (len < d.length + 3) { d.op = JvmOpcode::Invalid; return d; }
            uint16_t idx;
            std::memcpy(&idx, bytes + d.length, 2);
            d.operand_u32 = idx;
            d.switch_count = bytes[d.length + 2];   // reuse switch_count for dim count
            d.length += 3;
            break;
        }
        case OperandFormat::TableSwitch: {
            // 0-3 bytes padding to 4-byte alignment from method start.
            // For simplicity we assume the buffer starts at a 4-byte boundary
            // (the caller can adjust). In practice we need the absolute offset.
            // Here we use the relative offset within `bytes`.
            std::size_t pad = (4 - ((d.length) % 4)) % 4;
            if (len < d.length + pad + 12) { d.op = JvmOpcode::Invalid; return d; }
            std::size_t off = d.length + pad;
            int32_t default_val, low_val, high_val;
            std::memcpy(&default_val, bytes + off, 4);
            std::memcpy(&low_val, bytes + off + 4, 4);
            std::memcpy(&high_val, bytes + off + 8, 4);
            d.switch_default = default_val;
            d.switch_low = low_val;
            d.switch_high = high_val;
            d.switch_count = high_val - low_val + 1;
            d.switch_data = bytes + off + 12;
            d.length = static_cast<uint8_t>(off + 12 + d.switch_count * 4);
            break;
        }
        case OperandFormat::LookupSwitch: {
            std::size_t pad = (4 - ((d.length) % 4)) % 4;
            if (len < d.length + pad + 8) { d.op = JvmOpcode::Invalid; return d; }
            std::size_t off = d.length + pad;
            int32_t default_val, npairs;
            std::memcpy(&default_val, bytes + off, 4);
            std::memcpy(&npairs, bytes + off + 4, 4);
            d.switch_default = default_val;
            d.switch_count = npairs;
            d.switch_data = bytes + off + 8;
            d.length = static_cast<uint8_t>(off + 8 + npairs * 8);
            break;
        }
        case OperandFormat::Wide:
            // Should have been handled above.
            d.op = JvmOpcode::Invalid;
            return d;
    }
    return d;
}

}  // namespace jade::jvm
