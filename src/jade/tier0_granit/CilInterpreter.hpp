// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/CilInterpreter.hpp
//
// Real CIL bytecode interpreter for Tier 0 (granit).
//
// Exception-free hot path: all errors are returned via Result<Value>,
// not thrown. Uses a fixed-capacity eval stack (no vector realloc).
// Uses computed-goto dispatch for ~20-30% speedup on dispatch-heavy code.

#pragma once

#include "jade/core/Result.hpp"
#include "jade/cil/Opcode.hpp"
#include "jade/tier0_granit/Value.hpp"
#include "jade/runtime/Safepoint.hpp"

#include <array>
#include <span>
#include <cstdint>

namespace jade::granit {

// Maximum eval stack depth. CIL methods declare maxstack in their header;
// 256 is generous for typical methods.
constexpr std::size_t kMaxEvalStack = 256;

// ─────────────────────────────────────────────────────────────────────────────
// CilFrame — the execution frame for a CIL method.
// Uses a fixed-size array for the eval stack (no heap allocation).
// ─────────────────────────────────────────────────────────────────────────────
struct CilFrame {
    std::span<const uint8_t> il_code;
    uint32_t pc = 0;

    // Fixed-capacity eval stack (no vector realloc).
    std::array<Value, kMaxEvalStack> eval_stack;
    int32_t sp = 0;  // stack pointer (index into eval_stack)

    // Locals and args (still vectors — they're set up once per call).
    std::vector<Value> locals;
    std::vector<Value> args;

    // Error flag — set by helpers instead of throwing.
    // The main loop checks this after each op.
    bool error = false;
    const char* error_msg = nullptr;

    void push(Value v) {
        if (__builtin_expect(sp < static_cast<int32_t>(kMaxEvalStack), 1)) {
            eval_stack[sp++] = std::move(v);
        } else {
            error = true;
            error_msg = "CIL: stack overflow";
        }
    }

    [[nodiscard]] Value pop() {
        if (__builtin_expect(sp > 0, 1)) {
            return std::move(eval_stack[--sp]);
        }
        error = true;
        error_msg = "CIL: stack underflow";
        return Value::uninit();
    }

    [[nodiscard]] Value& top() {
        if (__builtin_expect(sp > 0, 1)) {
            return eval_stack[sp - 1];
        }
        static Value dummy = Value::uninit();
        error = true;
        error_msg = "CIL: stack empty";
        return dummy;
    }

    [[nodiscard]] bool has_error() const noexcept { return error; }
    void clear_error() noexcept { error = false; error_msg = nullptr; }
};

// ─────────────────────────────────────────────────────────────────────────────
// CilInterpreter — executes CIL bytecode.
// ─────────────────────────────────────────────────────────────────────────────
class CilInterpreter {
public:
    CilInterpreter() = default;

    [[nodiscard]] Result<Value> run(std::span<const uint8_t> il_code,
                                     uint16_t num_locals,
                                     uint16_t num_args,
                                     std::vector<Value> args = {});

    void set_safepoint_manager(SafepointManager* sm, SafepointManager::ThreadState* ts) noexcept {
        safepoint_mgr_ = sm;
        safepoint_state_ = ts;
    }

private:
    SafepointManager* safepoint_mgr_{nullptr};
    SafepointManager::ThreadState* safepoint_state_{nullptr};

    void poll_safepoint(CilFrame& frame);
};

}  // namespace jade::granit
