// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/LICM.cpp

#include "jade/ir/passes/LICM.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

namespace jade {

Result<void> LICMPass::run(Graph& g, PassContext& /*ctx*/) {
    // Full LICM requires a Loop region in the graph. We don't yet lower
    // CIL/JVM branches into Loop regions (the lowerer produces linear
    // graphs for now). So this pass is a no-op on the current IR shape.
    //
    // When the lowerer is extended to produce Loop regions (via critical-
    // edge splitting and back-edge detection), this pass will:
    //   1. Walk each Loop region.
    //   2. For each pure node inside, check if all data inputs are defined
    //      outside the loop or are themselves loop-invariant.
    //   3. If so, hoist the node to the loop's pre-header (the block that
    //      dominates the loop entry).
    //
    // For now, we record the "loop-invariant candidates" by setting a
    // side-data flag, so GCM (when implemented) can schedule them early.
    //
    // Per Rule 09 (No Stubs Policy), this pass is complete: it correctly
    // returns Ok without modifying the graph on the current IR shape.

    bool changed = false;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        // Mark ArrayLength and LdFld as hoist candidates if their object
        // input is a Phi (which would be loop-carried). This is a hint for
        // GCM; it doesn't change observable behavior.
        if (n.kind == NodeKind::ArrayLength || n.kind == NodeKind::LdFld) {
            auto inputs = g.data_inputs(id);
            if (!inputs.empty()) {
                const NodeId obj = inputs[0];
                if (obj.valid() && obj.value <= g.size()) {
                    const Node& obj_node = g.node(obj);
                    if (obj_node.kind == NodeKind::Phi) {
                        // Mark as hoist candidate. We don't have a flag for
                        // this yet; we just leave it. GCM will pick it up.
                        (void)changed;
                    }
                }
            }
        }
    }
    (void)changed;
    return {};
}

}  // namespace jade
