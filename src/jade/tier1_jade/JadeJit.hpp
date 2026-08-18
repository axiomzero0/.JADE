// SPDX-License-Identifier: MIT
// .JADE Compiler — tier1_jade/JadeJit.hpp
//
// Top-level driver for Tier 1 (JADE) compilation.
//
// Takes a Sea-of-Nodes Graph, allocates registers, emits x86-64 machine
// code, and returns a callable CompiledFunction.
//
// Per the Strict Error Handling policy, every fallible step returns
// Result<T>. If any step fails, the compilation is aborted and the
// caller (the dispatch loop) falls back to granit.

#pragma once

#include "jade/core/Result.hpp"
#include "jade/ir/Graph.hpp"
#include "jade/tier1_jade/LinearScanRegAlloc.hpp"
#include "jade/tier1_jade/CodeEmitter.hpp"

namespace jade::tier1 {

class JadeJit {
public:
    JadeJit();

    // Compile `graph` into a callable function.
    // Returns CompiledFunction on success, or Error on failure (caller
    // falls back to granit).
    [[nodiscard]] Result<CompiledFunction> compile(const Graph& graph);

    // Returns the most recent allocation result (for debugging / IR dump).
    [[nodiscard]] const std::optional<AllocationResult>& last_allocation() const noexcept {
        return last_alloc_;
    }

private:
    LinearScanRegAlloc allocator_;
    CodeEmitter        emitter_;
    std::optional<AllocationResult> last_alloc_;
};

}  // namespace jade::tier1
