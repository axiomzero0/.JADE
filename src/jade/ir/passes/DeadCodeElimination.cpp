// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/DeadCodeElimination.cpp

#include "jade/ir/passes/DeadCodeElimination.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <vector>

namespace jade {

namespace {

// Count uses of `id` across the entire graph, ignoring uses from nodes
// already marked dead. O(N) per call; for the initial milestone this is
// acceptable. A faster pass uses an explicit use list.
[[nodiscard]] uint32_t count_uses(const Graph& g, NodeId id) {
    uint32_t uses = 0;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId other{static_cast<uint32_t>(i + 1)};
        if (other == id) continue;
        // Skip uses from nodes already marked dead — they will be swept.
        if (g.node(other).is_dead()) continue;
        for (NodeId in : g.data_inputs(other)) {
            if (in == id) ++uses;
        }
        if (g.ctrl_input(other) == id) ++uses;
        if (g.effect_input(other) == id) ++uses;
    }
    return uses;
}

// An effectful node has "external" side effects if removing it would change
// observable program behavior (e.g., a Call, Throw, Return, or Safepoint).
// These nodes must NEVER be DCE'd even if they appear to have zero users.
// Effectful nodes like StoreField and Allocate CAN be DCE'd if they have
// zero users — their effect is only observable through other nodes that
// read their results.
[[nodiscard]] bool has_external_side_effects(NodeKind k) noexcept {
    switch (k) {
        case NodeKind::Call:
        case NodeKind::CallVirt:
        case NodeKind::CallKnown:
        case NodeKind::TailCall:
        case NodeKind::InvokeDynamic:
        case NodeKind::Return:
        case NodeKind::Throw:
        case NodeKind::Rethrow:
        case NodeKind::Leave:
        case NodeKind::EndFinally:
        case NodeKind::MonitorEnter:
        case NodeKind::MonitorExit:
        case NodeKind::Safepoint:
        case NodeKind::Deopt:
        case NodeKind::Unreachable:
            return true;
        default:
            return false;
    }
}

}  // namespace

Result<void> DeadCodeEliminationPass::run(Graph& g, PassContext& /*ctx*/) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            Node& n = g.node(id);
            if (n.is_dead()) continue;
            // Control-flow nodes are structural — never DCE.
            if (n.is_control()) continue;
            // Nodes with external side effects (Calls, Returns, Throws,
            // Safepoints) are never DCE'd — they have observable behavior.
            if (has_external_side_effects(n.kind)) continue;
            // Effectful nodes WITHOUT external side effects (StoreField,
            // Allocate, Box, StLoc, StFld, etc.) CAN be DCE'd if they have
            // zero users. Their effect is only observable through nodes that
            // read their results — if no one reads them, they're dead.
            // Pure nodes are always DCE-able if unused.

            if (count_uses(g, id) == 0) {
                g.mark_dead(id);
                changed = true;
            }
        }
    }
    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
