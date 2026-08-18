// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/Bytecode.hpp
//
// Bytecode opcodes consumed by the granit interpreter.
// Stack-based (register-style planned for the second milestone).

#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace jade::granit {

enum class Op : uint8_t {
    // ── Stack manipulation ─────────────────────────────────────────────
    Nop          = 0,
    PushConst0   = 1,    // push int 0
    PushConst1   = 2,    // push int 1
    PushConstI   = 3,    // push int (imm)
    PushConstF   = 4,    // push float (imm)
    PushConstN   = 5,    // push null
    Pop          = 6,
    Dup          = 7,
    Swap         = 8,

    // ── Arithmetic ─────────────────────────────────────────────────────
    Add          = 10,
    Sub          = 11,
    Mul          = 12,
    Div          = 13,
    Mod          = 14,
    Neg          = 15,
    And          = 16,
    Or           = 17,
    Xor          = 18,
    Not          = 19,
    Shl          = 20,
    Shr          = 21,

    // ── Comparisons ────────────────────────────────────────────────────
    Eq           = 30,
    Ne           = 31,
    Lt           = 32,
    Gt           = 33,
    Le           = 34,
    Ge           = 35,

    // ── Control flow ───────────────────────────────────────────────────
    Jump         = 40,   // unconditional, imm = target
    JumpIfTrue   = 41,
    JumpIfFalse  = 42,

    // ── Calls / return ──────────────────────────────────────────────────
    Call         = 50,   // imm = argc
    Return       = 51,

    // ── Locals ──────────────────────────────────────────────────────────
    LoadLocal    = 60,   // imm = slot
    StoreLocal   = 61,

    // ── Safepoint (granit polls on every loop back-edge and return) ──
    Safepoint    = 90,

    // ── Halt (used in tests) ──────────────────────────────────────────
    Halt         = 99,
};

[[nodiscard]] std::string_view op_name(Op op) noexcept;

struct Instruction {
    Op       op;
    int32_t  imm;   // immediate value (used by PushConstI, Jump, etc.)
};

using Program = std::vector<Instruction>;

// ─────────────────────────────────────────────────────────────────────────────
// ProgramBuilder — convenience for building small programs in tests.
// ─────────────────────────────────────────────────────────────────────────────
class ProgramBuilder {
public:
    ProgramBuilder& nop()            { prog_.push_back({Op::Nop, 0});          return *this; }
    ProgramBuilder& push_const_i(int32_t v) { prog_.push_back({Op::PushConstI, v}); return *this; }
    ProgramBuilder& push_const_f(double v) {
        int32_t imm;
        std::memcpy(&imm, &v, sizeof(imm));
        prog_.push_back({Op::PushConstF, imm});
        return *this;
    }
    ProgramBuilder& push_null()      { prog_.push_back({Op::PushConstN, 0});  return *this; }
    ProgramBuilder& pop()            { prog_.push_back({Op::Pop, 0});          return *this; }
    ProgramBuilder& dup()            { prog_.push_back({Op::Dup, 0});          return *this; }
    ProgramBuilder& swap()           { prog_.push_back({Op::Swap, 0});         return *this; }
    ProgramBuilder& add()            { prog_.push_back({Op::Add, 0});          return *this; }
    ProgramBuilder& sub()            { prog_.push_back({Op::Sub, 0});          return *this; }
    ProgramBuilder& mul()            { prog_.push_back({Op::Mul, 0});          return *this; }
    ProgramBuilder& div()            { prog_.push_back({Op::Div, 0});          return *this; }
    ProgramBuilder& mod()            { prog_.push_back({Op::Mod, 0});          return *this; }
    ProgramBuilder& neg()            { prog_.push_back({Op::Neg, 0});          return *this; }
    ProgramBuilder& eq()             { prog_.push_back({Op::Eq, 0});            return *this; }
    ProgramBuilder& ne()             { prog_.push_back({Op::Ne, 0});            return *this; }
    ProgramBuilder& lt()             { prog_.push_back({Op::Lt, 0});            return *this; }
    ProgramBuilder& gt()             { prog_.push_back({Op::Gt, 0});            return *this; }
    ProgramBuilder& jump(uint32_t target) { prog_.push_back({Op::Jump, static_cast<int32_t>(target)}); return *this; }
    ProgramBuilder& jump_if_true(uint32_t target) { prog_.push_back({Op::JumpIfTrue, static_cast<int32_t>(target)}); return *this; }
    ProgramBuilder& jump_if_false(uint32_t target) { prog_.push_back({Op::JumpIfFalse, static_cast<int32_t>(target)}); return *this; }
    ProgramBuilder& ret()           { prog_.push_back({Op::Return, 0});        return *this; }
    ProgramBuilder& safepoint()     { prog_.push_back({Op::Safepoint, 0});     return *this; }
    ProgramBuilder& halt()          { prog_.push_back({Op::Halt, 0});          return *this; }

    [[nodiscard]] Program build() const { return prog_; }
    [[nodiscard]] std::size_t size() const noexcept { return prog_.size(); }

private:
    Program prog_;
};

}  // namespace jade::granit
