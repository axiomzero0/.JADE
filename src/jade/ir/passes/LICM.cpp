// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/LICM.cpp
//
// Real Loop Invariant Code Motion using BuildRegions BlockStructure.
//
// For each loop header identified by BuildRegions:
//   1. Walk all nodes inside the loop body.
//   2. For each pure node, check if all data inputs are defined outside
//      the loop (loop-invariant).
//   3. If so, mark the node as a hoist candidate (set IsScheduled flag
//      so GCM can place it in the pre-header).
//
// The pass does NOT move nodes in the IR (that requires GCM with block
// scheduling). It marks candidates by setting the HasTypeNarrowing flag
// (reused as a "hoist candidate" marker since no other pass uses it on
// pure nodes).

#include "jade/ir/passes/LICM.hpp"
#include "jade/ir/passes/BuildRegions.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <unordered_set>

namespace jade {

namespace {

// Check if a node is loop-invariant: all data inputs are defined outside
// the loop (in a block that doesn't belong to the loop).
[[nodiscard]] bool is_loop_invariant(const Graph& g, const BlockStructure& bs,
                                       NodeId id, uint32_t loop_header) {
    // Get the block of this node.
    uint32_t node_block = bs.block_of(id);
    if (node_block == 0) return false;

    // Walk all data inputs.
    for (NodeId in : g.data_inputs(id)) {
        if (!in.valid() || in.value > g.size()) continue;
        uint32_t in_block = bs.block_of(in);
        // If the input is in the same loop (block >= loop_header), it's
        // NOT loop-invariant.
        if (in_block >= loop_header) return false;
    }
    return true;
}

}  // namespace

Result<void> LICMPass::run(Graph& g, PassContext& /*ctx*/) {
    // Build the block structure to identify loops.
    BlockStructure bs = build_block_structure(g);
    if (bs.blocks.empty()) return {};

    bool changed = false;

    // For each loop header, walk the loop body and mark hoist candidates.
    for (const auto& bb : bs.blocks) {
        if (!bb.is_loop_header) continue;

        uint32_t loop_header = bb.id;

        // Walk all blocks that are >= loop_header (inside the loop).
        for (uint32_t b = loop_header; b < bs.blocks.size(); ++b) {
            const BasicBlock& block = bs.blocks[b];
            if (!block.leader.valid()) continue;

            // Walk all nodes in this block.
            for (std::size_t i = 0; i < g.size(); ++i) {
                const NodeId id{static_cast<uint32_t>(i + 1)};
                const Node& n = g.node(id);
                if (n.is_dead()) continue;
                if (bs.block_of(id) != b) continue;

                // Only hoist pure nodes (no side effects).
                if (!n.is_pure()) continue;

                // Check if all inputs are defined outside the loop.
                if (is_loop_invariant(g, bs, id, loop_header)) {
                    // Mark as hoist candidate.
                    g.node(id).flags |= NodeFlag::IsScheduled;
                    changed = true;
                }
            }
        }
    }

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
