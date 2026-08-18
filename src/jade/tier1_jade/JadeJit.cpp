// SPDX-License-Identifier: MIT
// .JADE Compiler — tier1_jade/JadeJit.cpp

#include "jade/tier1_jade/JadeJit.hpp"
#include "jade/ir/Verifier.hpp"

#include <print>
#include <format>

namespace jade::tier1 {

JadeJit::JadeJit() = default;

Result<CompiledFunction> JadeJit::compile(const Graph& graph) {
    // Step 0: verify the graph (Rule 42) before doing any work on it.
    if (auto v = verify_graph(graph); !v) {
        return std::unexpected(make_error(
            ErrorKind::VerificationFailed,
            std::format("JadeJit: input graph failed verification: {}", v.error().what())));
    }

    // Step 1: allocate registers.
    auto alloc_r = allocator_.allocate(graph);
    if (!alloc_r) {
        std::println(stderr, "[WARN] JADE register allocation failed for graph: {}. "
                              "Falling back to granit.", alloc_r.error().what());
        return std::unexpected(alloc_r.error());
    }
    last_alloc_ = *alloc_r;

    // Step 2: emit machine code.
    auto emit_r = emitter_.emit(graph, *alloc_r);
    if (!emit_r) {
        std::println(stderr, "[WARN] JADE code emission failed for graph: {}. "
                              "Falling back to granit.", emit_r.error().what());
        return std::unexpected(emit_r.error());
    }

    return *emit_r;
}

}  // namespace jade::tier1
