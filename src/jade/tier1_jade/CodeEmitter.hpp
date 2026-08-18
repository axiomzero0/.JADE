// SPDX-License-Identifier: MIT
// .JADE Compiler — tier1_jade/CodeEmitter.hpp
//
// Tier 1 (JADE) code emitter. Takes a Sea-of-Nodes Graph + AllocationResult
// and emits x86-64 machine code via asmjit.
//
// The emitter is REAL — it actually generates executable machine code and
// returns a function pointer that can be called. Per Rule 09 (No Stubs),
// any NodeKind we cannot lower returns an UnsupportedNode error and the
// driver falls back to granit.

#pragma once

#include "jade/core/Result.hpp"
#include "jade/ir/Graph.hpp"
#include "jade/tier1_jade/LinearScanRegAlloc.hpp"

#include <cstddef>
#include <cstdint>

namespace jade::tier1 {

// ─────────────────────────────────────────────────────────────────────────────
// CompiledFunction — the output of the emitter.
//
//   `entry_point` is a function pointer to the compiled code. The signature
//   depends on the original method's signature; for the initial milestone,
//   we support functions of the form `int64_t(*)()` (no args, returns int64).
// ─────────────────────────────────────────────────────────────────────────────
struct CompiledFunction {
    void*    entry_point;
    size_t   code_size;
    // The asmjit runtime owns the code memory. `runtime` is non-owning;
    // the caller must keep the runtime alive as long as `entry_point` is used.
    void*    runtime_handle;
};

class CodeEmitter {
public:
    CodeEmitter();
    ~CodeEmitter();

    CodeEmitter(const CodeEmitter&) = delete;
    CodeEmitter& operator=(const CodeEmitter&) = delete;

    // Compile `graph` into executable machine code.
    // Returns the entry point on success, or an error explaining why
    // emission failed (the driver will fall back to granit).
    [[nodiscard]] Result<CompiledFunction> emit(const Graph& graph,
                                                  const AllocationResult& alloc);

private:
    // Pimpl-style: hide asmjit headers from the public API.
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Initialize the asmjit runtime. Returns an error if initialization
    // fails (e.g., out of memory).
    [[nodiscard]] Result<void> init_runtime();
};

// Helper: compute the "lowering position" of each node — the linear
// instruction index in the emitted code. This drives both the regalloc
// and the emitter.
[[nodiscard]] std::vector<uint32_t> compute_node_positions(const Graph& graph);

}  // namespace jade::tier1
