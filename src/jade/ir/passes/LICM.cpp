// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/LICM.cpp
//
// Real Loop Invariant Code Motion.
//
// Uses BuildRegions to identify loop headers. For each pure node inside
// a loop whose data inputs are all defined outside the loop:
//   1. Mark the node with IsScheduled (hoist candidate flag).
//   2. If the node is a LoadField/ArrayLength whose object is loop-invariant,
//      mark it as hoistable — GCM will schedule it in the pre-header.
//
// Also performs:
//   - ArrayLength hoisting: if arr is loop-invariant, hoist arr.length.
//   - Constant hoisting: if a ConstInt is inside a loop, mark it hoistable
//     (it's trivially invariant).
//   - Pure arithmetic hoisting: if Add/Sub/Mul/etc has all inputs outside
//     the loop, mark it hoistable.
//
// The pass marks nodes; GCM performs the actual scheduling (moving the
// node to the pre-header block). When GCM is fully block-scheduled, the
// hoisted nodes will be placed before the loop header.

#include "jade/ir/passes/LICM.hpp"
#include "jade/ir/passes/BuildRegions.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <unordered_set>

namespace jade {

namespace {

[[nodiscard]] bool is_loop_invariant(const Graph& g, const BlockStructure& bs,
                                       NodeId id, uint32_t loop_header) {
    uint32_t node_block = bs.block_of(id);
    if (node_block == 0) return false;

    for (NodeId in : g.data_inputs(id)) {
        if (!in.valid() || in.value > g.size()) continue;
        uint32_t in_block = bs.block_of(in);
        if (in_block >= loop_header) return false;
    }
    return true;
}

// Check if a node is safe to hoist: must be pure (no side effects),
// and must not be a control/effect node.
[[nodiscard]] bool is_hoistable(const Node& n) noexcept {
    if (!n.is_pure()) return false;
    if (n.is_effect()) return false;
    if (n.is_control()) return false;
    // Phi nodes are NOT hoistable (they're tied to the merge point).
    if (n.kind == NodeKind::Phi) return false;
    return true;
}

}  // namespace

Result<void> LICMPass::run(Graph& g, PassContext& /*ctx*/) {
    BlockStructure bs = build_block_structure(g);
    if (bs.blocks.empty()) return {};

    bool changed = false;

    for (const auto& bb : bs.blocks) {
        if (!bb.is_loop_header) continue;

        uint32_t loop_header = bb.id;

        // Walk all blocks inside the loop (block ID >= loop_header).
        for (uint32_t b = loop_header; b < bs.blocks.size(); ++b) {
            const BasicBlock& block = bs.blocks[b];
            if (!block.leader.valid()) continue;

            // Walk all nodes in this block.
            for (std::size_t i = 0; i < g.size(); ++i) {
                const NodeId id{static_cast<uint32_t>(i + 1)};
                const Node& n = g.node(id);
                if (n.is_dead()) continue;
                if (bs.block_of(id) != b) continue;

                if (!is_hoistable(n)) continue;
                if (!is_loop_invariant(g, bs, id, loop_header)) continue;

                // Don't re-mark already-scheduled nodes.
                if (n.flags.has(NodeFlag::IsScheduled)) continue;

                // Mark as hoist candidate for GCM.
                g.node(id).flags |= NodeFlag::IsScheduled;
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
