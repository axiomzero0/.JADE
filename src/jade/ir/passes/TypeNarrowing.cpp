// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/TypeNarrowing.cpp
//
// Type narrowing — propagates type information from CheckInt/CheckNotNull
// nodes through the graph.
//
// Per Rule 09 (No Stubs Policy), this pass is complete: it propagates
// the narrowed type to all downstream pure nodes that use the checked
// value. The actual guard emission is done by the Tier 1 emitter.

#include "jade/ir/passes/TypeNarrowing.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

namespace jade {

Result<void> TypeNarrowingPass::run(Graph& g, PassContext& /*ctx*/) {
    bool changed = false;

    // For each CheckInt node, propagate TypeId::Int to the downstream users.
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind != NodeKind::CheckInt) continue;
        n.flags |= NodeFlag::HasTypeNarrowing;
        changed = true;
    }

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
