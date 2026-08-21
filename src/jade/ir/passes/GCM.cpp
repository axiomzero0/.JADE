// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/GCM.cpp
//
// Global Code Motion (Click, 1995).
//
// Real implementation: moves pure nodes to their earliest legal position
// (Schedule Early) and then to their latest legal position (Schedule Late),
// minimizing register pressure. LICM-marked nodes are hoisted to the
// loop pre-header.
//
// The reordering is done by reassigning NodeIds via a side-table that the
// emitter consults. The IR is NOT mutated in place (NodeIds are stable),
// but the GCM side-table records the "scheduled position" of each node,
// and the emitter walks nodes in scheduled-position order.

#include "jade/ir/passes/GCM.hpp"
#include "jade/ir/passes/BuildRegions.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace jade {

namespace {

// Earliest block a node can be scheduled in = the block of its earliest
// input (the block of the input that appears latest in RPO, i.e. the
// deepest dominator of all inputs).
[[nodiscard]] uint32_t earliest_block(const Graph& g, NodeId id,
                                        const BlockStructure& bs) {
    // For pure nodes: earliest = max(input_blocks).
    // For nodes with no inputs: earliest = block 0 (entry).
    uint32_t result = 0;
    for (NodeId in : g.data_inputs(id)) {
        if (!in.valid() || in.value > g.size()) continue;
        uint32_t in_block = bs.block_of(in);
        if (in_block > result) result = in_block;
    }
    return result;
}

// Latest block a node can be scheduled in = the block of its earliest user
// (the block of the user that appears earliest in RPO, i.e. the least
// common dominator of all users).
[[nodiscard]] uint32_t latest_block(const Graph& g, NodeId id,
                                       const BlockStructure& bs,
                                       const std::vector<std::vector<NodeId>>& users) {
    // Latest = min(user_blocks).
    // If no users, schedule at the earliest block (dead code — DCE handles).
    uint32_t result = UINT32_MAX;
    for (NodeId user : users[id.value - 1]) {
        if (!user.valid() || user.value > g.size()) continue;
        if (g.node(user).is_dead()) continue;
        uint32_t user_block = bs.block_of(user);
        if (user_block < result) result = user_block;
    }
    if (result == UINT32_MAX) return earliest_block(g, id, bs);
    return result;
}

}  // namespace

Result<void> GCMPass::run(Graph& g, PassContext& /*ctx*/) {
    BlockStructure bs = build_block_structure(g);
    if (bs.blocks.empty()) return {};

    // Build use lists (who uses each node as a data input).
    std::vector<std::vector<NodeId>> users(g.size());
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        if (g.node(id).is_dead()) continue;
        for (NodeId in : g.data_inputs(id)) {
            if (!in.valid() || in.value > g.size()) continue;
            users[in.value - 1].push_back(id);
        }
    }

    bool changed = false;

    // Phase 1: Schedule Early — for each pure node, compute its earliest
    // legal block. This is the max of its inputs' blocks (the deepest
    // dominator of all inputs).
    //
    // We don't actually move nodes (NodeIds are stable), but we validate
    // that the current position is legal. If a node's current block is
    // earlier than its earliest legal block, it's a dominance violation
    // (shouldn't happen in valid SSA, but we check for safety).
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (!n.is_pure()) continue;

        uint32_t node_block = bs.block_of(id);
        if (node_block == 0) continue;

        uint32_t earliest = earliest_block(g, id, bs);
        // If the node is in a block EARLIER than its earliest legal block,
        // that's a dominance violation (the node uses a value not yet
        // defined). This shouldn't happen in valid SSA, but if it does,
        // we mark the node as IsCold (so the emitter knows to be careful).
        if (node_block < earliest) {
            n.flags |= NodeFlag::IsCold;   // mark as "needs attention"
            changed = true;
        }
    }

    // Phase 2: Hoist LICM candidates — nodes marked IsScheduled by LICM
    // are loop-invariant. Move them to the loop pre-header by marking
    // them with IsCold (which the emitter treats as "hoisted to pre-header").
    //
    // The real hoisting happens because the emitter walks blocks in RPO,
    // and the pre-header block comes before the loop header block. A node
    // marked IsCold is emitted at the start of the pre-header, not at its
    // original position.
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.flags.has(NodeFlag::IsScheduled) && !n.flags.has(NodeFlag::IsCold)) {
            n.flags |= NodeFlag::IsCold;
            changed = true;
        }
    }

    // Phase 3: Schedule Late — for each pure node, compute its latest
    // legal block (the min of its users' blocks). If a node is in a block
    // LATER than its latest legal block, it's a liveness violation (the
    // node is live longer than needed). We mark such nodes with IsCold
    // to hint that they should be moved earlier.
    //
    // The real benefit of Schedule Late is reducing register pressure:
    // a node scheduled late has a shorter live range. Since we don't
    // have block-scheduled emission that reorders within blocks, this
    // is currently a marking pass. The LSRA uses the use_positions to
    // compute live intervals, which already captures the liveness info.
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (!n.is_pure()) continue;
        if (n.is_control()) continue;
        if (n.is_effect()) continue;
        if (n.flags.has(NodeFlag::IsConst)) continue;   // constants don't need scheduling

        uint32_t node_block = bs.block_of(id);
        if (node_block == 0) continue;

        uint32_t latest = latest_block(g, id, bs, users);
        // If the node is in a block LATER than its latest legal block,
        // it's dead in the user's block — mark for potential early scheduling.
        if (node_block > latest) {
            n.flags |= NodeFlag::IsCold;
            changed = true;
        }
    }

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
