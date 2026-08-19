// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/CilInterpreter.hpp
//
// Real CIL bytecode interpreter for Tier 0 (granit).
//
// Executes decoded CIL opcodes (from src/jade/cil/Opcode.hpp) on a typed
// evaluation stack. This is NOT the legacy Op-enum interpreter — it consumes
// real ECMA-335 CIL byte streams.
//
// Per Rule 09 (No Stubs Policy), every implemented opcode is fully functional.
// Unsupported opcodes return an error; the driver falls back to the legacy
// interpreter or aborts.

#pragma once

#include "jade/core/Result.hpp"
#include "jade/cil/Opcode.hpp"
#include "jade/tier0_granit/Value.hpp"
#include "jade/runtime/Safepoint.hpp"

#include <vector>
#include <span>
#include <cstdint>

namespace jade::granit {

// ─────────────────────────────────────────────────────────────────────────────
// CilFrame — the execution frame for a CIL method.
// ─────────────────────────────────────────────────────────────────────────────
struct CilFrame {
    std::span<const uint8_t> il_code;        // the IL byte stream
    uint32_t pc = 0;                          // program counter (byte offset)
    std::vector<Value> eval_stack;            // evaluation stack
    std::vector<Value> locals;                // local variable slots
    std::vector<Value> args;                  // method arguments

    void push(Value v) { eval_stack.push_back(std::move(v)); }
    [[nodiscard]] Value pop() {
        if (eval_stack.empty()) throw std::runtime_error("CIL: stack underflow");
        Value v = std::move(eval_stack.back());
        eval_stack.pop_back();
        return v;
    }
    [[nodiscard]] Value& top() {
        if (eval_stack.empty()) throw std::runtime_error("CIL: stack empty");
        return eval_stack.back();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// CilInterpreter — executes CIL bytecode.
// ─────────────────────────────────────────────────────────────────────────────
class CilInterpreter {
public:
    CilInterpreter() = default;

    // Execute `il_code` with the given locals/args count. Returns the
    // return value (or an error).
    [[nodiscard]] Result<Value> run(std::span<const uint8_t> il_code,
                                     uint16_t num_locals,
                                     uint16_t num_args,
                                     std::vector<Value> args = {});

    // Inject a SafepointManager for GC/JIT polling.
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
