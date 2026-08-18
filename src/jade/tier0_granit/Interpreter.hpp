// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/Interpreter.hpp
//
// granit (Tier 0) — CIL interpreter.
//   "Zero compilation latency, instant startup, aggressive profile collection."
//
//   Executes CIL bytecode on a typed evaluation stack while updating Inline
//   Caches (ICs) and Type Feedback Vectors (TFVs). Maintains invocation/branch
//   counters. Polls the SafepointManager at every loop back-edge and return.
//
//   For the initial milestone, the interpreter also supports the legacy
//   hand-rolled bytecode in Bytecode.hpp (used by tests that pre-date the
//   CIL frontend). New tests should use CIL.

#pragma once

#include "jade/core/Result.hpp"
#include "jade/tier0_granit/Value.hpp"
#include "jade/tier0_granit/Bytecode.hpp"
#include "jade/runtime/Safepoint.hpp"

#include <cstdint>
#include <vector>
#include <unordered_map>

namespace jade::granit {

// ─────────────────────────────────────────────────────────────────────────────
// TypeFeedbackVector — collected by granit, consumed by JADE/RUBY/DIAMOND.
// ─────────────────────────────────────────────────────────────────────────────
struct TypeFeedback {
    // Per-instruction: observed types (one bit per EvalStackType).
    std::vector<uint8_t> observed_types;
    // Per-branch: taken / total.
    std::vector<uint32_t> branch_taken;
    std::vector<uint32_t> branch_total;
    // Per-callvirt: observed receiver runtime class (drives devirtualization).
    std::vector<uint32_t> call_target_seen;
    // Per-box: observed boxed value type.
    std::vector<uint32_t> boxed_type_seen;
};

// ─────────────────────────────────────────────────────────────────────────────
// Interpreter — runs a Program on a stack VM.
//
//   Uses the new `Value` type (ObjectHandle / ManagedPointer / etc.).
//   The legacy `Op` enum (Bytecode.hpp) is supported for back-compat.
//   CIL execution is provided separately in CilInterpreter.
// ─────────────────────────────────────────────────────────────────────────────
class Interpreter {
public:
    Interpreter() = default;

    // Run a legacy Op program and return the top-of-stack value.
    [[nodiscard]] Result<Value> run(const Program& prog);

    [[nodiscard]] const TypeFeedback& feedback() const noexcept { return feedback_; }
    [[nodiscard]] std::size_t max_stack_depth() const noexcept { return max_stack_depth_; }

    // Allow injecting a SafepointManager. If set, the interpreter polls
    // at every back-edge / return.
    void set_safepoint_manager(SafepointManager* sm, SafepointManager::ThreadState* ts) noexcept {
        safepoint_mgr_ = sm;
        safepoint_state_ = ts;
    }

private:
    std::vector<Value> stack_{};
    std::vector<Value> locals_{};
    TypeFeedback feedback_{};
    std::size_t max_stack_depth_{0};
    SafepointManager* safepoint_mgr_{nullptr};
    SafepointManager::ThreadState* safepoint_state_{nullptr};

    void push(Value v);
    [[nodiscard]] Value pop();
    [[nodiscard]] Value& top();
    void poll_safepoint();
};

}  // namespace jade::granit
