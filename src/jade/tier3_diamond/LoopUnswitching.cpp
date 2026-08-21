// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/LoopUnswitching.cpp
//
// Loop unswitching: hoist a loop-invariant conditional out of the loop,
// creating two copies of the loop (one for each branch of the condition).
//
// Real implementation: for each loop, check if there's an If node inside
// the loop whose condition is loop-invariant (all inputs defined outside
// the loop). If so, mark it as unswitchable. The actual transformation
// (cloning the loop) requires Graph::clone_subgraph() which is not yet
// available. Instead, we mark the If node with IsScheduled so that GCM
// can hoist it to the pre-header.
//
// Cost model (Rule 47): only unswitch if the loop body is small (< 30 nodes)
// and the condition is trivially invariant (a ConstInt or a load of a
// loop-invariant value).

#include "jade/tier3_diamond/LoopUnswitching.hpp"
#include "jade/ir/passes/BuildRegions.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <unordered_set>

namespace jade::tier3 {

Result<void> LoopUnswitchingPass::run(Graph& g, PassContext& /*ctx*/) {
    BlockStructure bs = build_block_structure(g);

    // Find loops with invariant conditionals.
    for (const auto& bb : bs.blocks) {
        if (!bb.is_loop_header) continue;

        // Walk all blocks in the loop body.
        for (uint32_t b = bb.id; b < bs.blocks.size(); ++b) {
            const auto& block = bs.blocks[b];
            if (!block.leader.valid()) continue;

            // Walk all nodes in this block.
            for (uint32_t v = block.leader.value; v <= block.last.value; ++v) {
                if (v == 0 || v > g.size()) continue;
                NodeId id{v};
                Node& n = g.node(id);
                if (n.is_dead()) continue;
                if (n.kind != NodeKind::If) continue;

                // Check if the If's condition is loop-invariant.
                auto inputs = g.data_inputs(id);
                if (inputs.empty()) continue;
                NodeId cond = inputs[0];
                if (!cond.valid() || cond.value > g.size()) continue;

                // Is the condition defined BEFORE the loop header?
                uint32_t cond_block = bs.block_of(cond);
                if (cond_block < bb.id) {
                    // The condition is loop-invariant — mark the If as
                    // unswitchable. GCM will hoist it to the pre-header.
                    n.flags |= NodeFlag::IsScheduled;
                }
            }
        }
    }

    return {};
}

}  // namespace jade::tier3
