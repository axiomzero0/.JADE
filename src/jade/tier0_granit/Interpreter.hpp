// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/Interpreter.hpp
//
// granit (Tier 0) — register-style interpreter.
//   "Zero compilation latency, instant startup, aggressive profile collection."
//
//   Executes stack bytecode while updating Inline Caches (ICs) and Type
//   Feedback Vectors (TFVs). Maintains invocation/branch counters. Polls
//   the SafepointManager at every loop back-edge and return.

#pragma once

#include "jade/core/Result.hpp"
#include "jade/tier0_granit/Bytecode.hpp"
#include "jade/runtime/Safepoint.hpp"

#include <cstdint>
#include <vector>
#include <variant>
#include <unordered_map>

namespace jade::granit {

// ─────────────────────────────────────────────────────────────────────────────
// Value — tagged union of all runtime types.
// Uses NaN-boxing in the real implementation; for the initial milestone a
// std::variant is used for clarity.
// ─────────────────────────────────────────────────────────────────────────────
using Value = std::variant<int64_t, double, bool, std::nullptr_t>;

[[nodiscard]] std::string to_string(Value v);

// ─────────────────────────────────────────────────────────────────────────────
// TypeFeedbackVector — collected by granit, consumed by JADE/RUBY/DIAMOND.
// ─────────────────────────────────────────────────────────────────────────────
struct TypeFeedback {
    // Per-instruction: observed types (one bit per TypeId).
    std::vector<uint16_t> observed_types;
    // Per-branch: taken / total.
    std::vector<uint32_t> branch_taken;
    std::vector<uint32_t> branch_total;
};

// ─────────────────────────────────────────────────────────────────────────────
// Interpreter — runs a Program on a stack VM.
// ─────────────────────────────────────────────────────────────────────────────
class Interpreter {
public:
    Interpreter() = default;

    // Run `prog` and return the top-of-stack value (or an error).
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
