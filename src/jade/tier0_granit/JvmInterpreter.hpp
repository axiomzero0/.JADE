// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/JvmInterpreter.hpp
//
// Real JVM bytecode interpreter for Tier 0 (granit).
//
// Executes decoded JVM opcodes (from src/jade/jvm/Opcode.hpp) on a typed
// evaluation stack. This is NOT the legacy Op-enum interpreter — it consumes
// real JVMS §6.5 byte streams.

#pragma once

#include "jade/core/Result.hpp"
#include "jade/jvm/Opcode.hpp"
#include "jade/tier0_granit/Value.hpp"
#include "jade/runtime/Safepoint.hpp"

#include <vector>
#include <span>
#include <cstdint>

namespace jade::granit {

struct JvmFrame {
    std::span<const uint8_t> code;
    uint32_t pc = 0;
    std::vector<Value> stack;
    std::vector<Value> locals;

    void push(Value v) { stack.push_back(std::move(v)); }
    [[nodiscard]] Value pop() {
        if (stack.empty()) throw std::runtime_error("JVM: stack underflow");
        Value v = std::move(stack.back());
        stack.pop_back();
        return v;
    }
    [[nodiscard]] Value& top() {
        if (stack.empty()) throw std::runtime_error("JVM: stack empty");
        return stack.back();
    }
};

class JvmInterpreter {
public:
    JvmInterpreter() = default;

    [[nodiscard]] Result<Value> run(std::span<const uint8_t> code,
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
    void poll_safepoint();
};

}  // namespace jade::granit
