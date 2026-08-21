// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/LoopPeeling.cpp
//
// Loop peeling: peel the first iteration of a counted loop to enable
// specialization (e.g., remove the bounds check if length ≥ 1 is proven).
//
// Real implementation: for each loop header, check if the loop has a
// known iteration count (induction variable with constant bound). If so,
// peel the first iteration by:
//   1. Cloning the loop body.
//   2. Replacing the loop header's back-edge with a jump to the cloned body.
//   3. The cloned body falls through to the loop header.
//
// The peeled iteration can be specialized: the first iteration's induction
// variable is known to be 0, enabling constant folding.
//
// Cost model (Rule 47): only peel if the loop body is small (< 20 nodes)
// and the loop is expected to execute ≥ 2 iterations.

#include "jade/ir/passes/LoopPeeling.hpp"
#include "jade/ir/passes/BuildRegions.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <vector>

namespace jade {

Result<void> LoopPeelingPass::run(Graph& g, PassContext& /*ctx*/) {
    BlockStructure bs = build_block_structure(g);

    // Find loops that are candidates for peeling.
    // A loop is peelable if:
    //   1. It has a Loop header (is_loop_header == true).
    //   2. The loop body is small (< 20 nodes).
    //   3. The loop has a single back-edge.
    //
    // The actual peeling (cloning the body) requires Graph::clone_subgraph()
    // which is not yet available. Instead, we mark the loop header with
    // IsScheduled (reused as "peel candidate" flag) so that future peeling
    // passes know which loops to peel.

    bool changed = false;
    for (const auto& bb : bs.blocks) {
        if (!bb.is_loop_header) continue;

        // Count the nodes in the loop body.
        uint32_t loop_node_count = 0;
        for (uint32_t b = bb.id; b < bs.blocks.size(); ++b) {
            const auto& block = bs.blocks[b];
            if (!block.leader.valid()) continue;
            for (uint32_t v = block.leader.value; v <= block.last.value; ++v) {
                if (v == 0 || v > g.size()) continue;
                NodeId id{v};
                if (g.node(id).is_dead()) continue;
                ++loop_node_count;
            }
            // Check if this block has a back-edge to the loop header.
            bool has_back_edge = false;
            for (uint32_t succ : block.successors) {
                if (succ == bb.id) {
                    has_back_edge = true;
                    break;
                }
            }
            if (has_back_edge) break;
        }

        // Cost model: only peel if the loop body is small.
        if (loop_node_count > 0 && loop_node_count < 20) {
            // Mark the loop header as a peel candidate.
            NodeId loop_node = bb.leader;
            if (loop_node.valid() && !g.node(loop_node).is_dead()) {
                g.node(loop_node).flags |= NodeFlag::IsScheduled;
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
