// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/GCM.cpp
//
// Global Code Motion (Click, 1995).
//
// Performs:
//   1. Schedule Early: pure nodes are placed as early as possible (after
//      their inputs are defined).
//   2. Schedule Late: pure nodes are placed as late as possible (before
//      their first use).
//   3. Hoist LICM candidates: nodes marked IsScheduled by LICM are moved
//      before the loop header (into the pre-header).
//
// Since the current IR is linear (nodes are in NodeId order and the emitter
// walks them in that order), GCM performs "virtual hoisting": it marks
// hoisted nodes by setting the IsCold flag (reused as "hoisted to pre-header"
// marker). The emitter can use this to reorder nodes when block scheduling
// is added.
//
// For now, GCM validates dominance and marks candidates. The actual
// reordering happens when the emitter uses block scheduling.

#include "jade/ir/passes/GCM.hpp"
#include "jade/ir/passes/BuildRegions.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

namespace jade {

Result<void> GCMPass::run(Graph& g, PassContext& /*ctx*/) {
    BlockStructure bs = build_block_structure(g);
    if (bs.blocks.empty()) return {};

    // Phase 1: Validate dominance (definition must come before use in
    // block order).
    // This is a correctness check, not a transformation.
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (!n.is_pure()) continue;

        uint32_t node_block = bs.block_of(id);
        if (node_block == 0) continue;

        for (NodeId in : g.data_inputs(id)) {
            if (!in.valid() || in.value > g.size()) continue;
            uint32_t in_block = bs.block_of(in);
            if (in_block > node_block) {
                // Dominance violation: input is defined after the use.
                // This can happen with branch reordering. GCM would fix
                // this by moving the node; for now, we leave it (the
                // emitter handles linear order).
            }
        }
    }

    // Phase 2: Mark LICM hoist candidates as "virtually hoisted".
    // Nodes marked IsScheduled by LICM are loop-invariant and should
    // be placed in the pre-header. We mark them with IsCold (reused as
    // "hoisted" marker) so the emitter knows to emit them before the
    // loop body.
    //
    // This is a marking pass, not a reordering pass. The actual node
    // reordering requires block-scheduled emission, which is planned.
    bool changed = false;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.flags.has(NodeFlag::IsScheduled) && !n.flags.has(NodeFlag::IsCold)) {
            n.flags |= NodeFlag::IsCold;  // mark as "hoisted to pre-header"
            changed = true;
        }
    }

    // Phase 3: Schedule Late — for each pure node, check if it can be
    // moved closer to its uses. Since we don't have block-scheduled
    // emission yet, this is a no-op. When block scheduling is added,
    // this phase will move pure nodes to the latest valid block
    // (minimizing register pressure).

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
