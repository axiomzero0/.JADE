// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/JvmInterpreter.hpp
//
// Real JVM bytecode interpreter — exception-free hot path.
// Uses fixed-capacity eval stack (no vector realloc).

#pragma once

#include "jade/core/Result.hpp"
#include "jade/jvm/Opcode.hpp"
#include "jade/tier0_granit/Value.hpp"
#include "jade/runtime/Safepoint.hpp"

#include <array>
#include <span>
#include <cstdint>
#include <vector>

namespace jade::granit {

constexpr std::size_t kMaxJvmStack = 256;

struct JvmFrame {
    std::span<const uint8_t> code;
    uint32_t pc = 0;
    std::array<Value, kMaxJvmStack> stack;
    int32_t sp = 0;
    std::vector<Value> locals;

    bool error = false;
    const char* error_msg = nullptr;

    void push(Value v) {
        if (__builtin_expect(sp < static_cast<int32_t>(kMaxJvmStack), 1))
            stack[sp++] = std::move(v);
        else { error = true; error_msg = "JVM: stack overflow"; }
    }
    [[nodiscard]] Value pop() {
        if (__builtin_expect(sp > 0, 1)) return std::move(stack[--sp]);
        error = true; error_msg = "JVM: stack underflow";
        return Value::uninit();
    }
    [[nodiscard]] Value& top() {
        if (__builtin_expect(sp > 0, 1)) return stack[sp - 1];
        static Value dummy = Value::uninit();
        error = true; error_msg = "JVM: stack empty";
        return dummy;
    }
    [[nodiscard]] bool has_error() const noexcept { return error; }
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
