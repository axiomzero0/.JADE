// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/Bytecode.cpp

#include "jade/tier0_granit/Bytecode.hpp"
#include <cstring>

namespace jade::granit {

namespace {

constexpr std::string_view kOpNames[] = {
    "Nop",          // 0
    "PushConst0",   // 1
    "PushConst1",   // 2
    "PushConstI",   // 3
    "PushConstF",   // 4
    "PushConstN",   // 5
    "Pop",          // 6
    "Dup",          // 7
    "Swap",         // 8
    "<reserved 9>",
    "Add",          // 10
    "Sub",          // 11
    "Mul",          // 12
    "Div",          // 13
    "Mod",          // 14
    "Neg",          // 15
    "And",          // 16
    "Or",           // 17
    "Xor",          // 18
    "Not",          // 19
    "Shl",          // 20
    "Shr",          // 21
    "<reserved 22-29>",
    "Eq",           // 30
    "Ne",           // 31
    "Lt",           // 32
    "Gt",           // 33
    "Le",           // 34
    "Ge",           // 35
    "<reserved 36-39>",
    "Jump",         // 40
    "JumpIfTrue",   // 41
    "JumpIfFalse",  // 42
    "<reserved 43-49>",
    "Call",         // 50
    "Return",       // 51
    "<reserved 52-59>",
    "LoadLocal",    // 60
    "StoreLocal",   // 61
    "<reserved 62-89>",
    "Safepoint",    // 90
    "<reserved 91-98>",
    "Halt",         // 99
};

}  // namespace

std::string_view op_name(Op op) noexcept {
    const auto idx = static_cast<std::size_t>(op);
    if (idx < std::size(kOpNames)) return kOpNames[idx];
    return "Unknown";
}

}  // namespace jade::granit
