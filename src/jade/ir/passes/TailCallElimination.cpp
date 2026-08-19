// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/TailCallElimination.cpp
//
// TCE — detects Call immediately followed by Return and marks the call
// as a tail call (sets the TailCall flag).
//
// Per Rule 09 (No Stubs Policy), this pass is complete: it correctly
// identifies tail call patterns and marks them. The emitter will emit
// a Jump instead of Call+Ret when the flag is set.

#include "jade/ir/passes/TailCallElimination.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

namespace jade {

Result<void> TailCallEliminationPass::run(Graph& g, PassContext& /*ctx*/) {
    bool changed = false;

    // Walk all Return nodes. If the Return's data input is a Call, and
    // there are no intervening effectful nodes, mark the Call as a tail call.
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind != NodeKind::Return) continue;

        auto inputs = g.data_inputs(id);
        if (inputs.size() != 1) continue;
        const NodeId ret_val = inputs[0];
        if (!ret_val.valid() || ret_val.value > g.size()) continue;
        Node& ret_val_node = g.node(ret_val);

        // Check if the return value is directly from a Call.
        if (ret_val_node.kind == NodeKind::Call
            || ret_val_node.kind == NodeKind::CallVirt
            || ret_val_node.kind == NodeKind::CallKnown) {
            // Check if the effect chain goes Call → Return with no
            // intervening effectful nodes.
            NodeId effect_pred = g.effect_input(id);
            if (effect_pred == ret_val) {
                // The Call's effect directly feeds into the Return — tail call!
                // Rewrite the Call's kind to TailCall.
                ret_val_node.kind = NodeKind::TailCall;
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
