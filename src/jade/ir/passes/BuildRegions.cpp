// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/BuildRegions.cpp
//
// Implements the BuildRegions pass: identifies basic blocks, connects
// control-flow edges, detects loops, and computes the dominator tree.
//
// This is the single highest-ROI pass — it unblocks LICM, GCM,
// LoopUnrolling, LoopPeeling, LoopUnswitching, PEA-materialization,
// and SRA-with-Phi.

#include "jade/ir/passes/BuildRegions.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"

#include <algorithm>
#include <unordered_set>

namespace jade {

// ─────────────────────────────────────────────────────────────────────────────
// Phase 1: Identify basic blocks.
//
//   A "block leader" is a node that starts a new basic block:
//   - Start (entry)
//   - IfTrue / IfFalse (branch targets)
//   - Region (merge point)
//   - Loop (loop header)
//   - Jump (unconditional branch target — a new block starts after Jump)
//   - Return (a new block starts after Return, if there are more nodes)
//
//   We walk all nodes in NodeId order. When we encounter a leader, we
//   start a new block. All subsequent non-leader nodes belong to the
//   current block until we hit another leader.
// ─────────────────────────────────────────────────────────────────────────────

void BuildRegionsPass::identify_blocks(const Graph& graph, BlockStructure& bs) {
    if (graph.size() == 0) return;

    // First pass: identify all leaders.
    std::vector<bool> is_leader(graph.size() + 1, false);

    for (std::size_t i = 0; i < graph.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = graph.node(id);
        if (n.is_dead()) continue;

        switch (n.kind) {
            case NodeKind::Start:
            case NodeKind::IfTrue:
            case NodeKind::IfFalse:
            case NodeKind::Region:
            case NodeKind::Loop:
                is_leader[id.value] = true;
                break;
            case NodeKind::Return:
            case NodeKind::Throw:
            case NodeKind::Jump:
                // These END a block — the next node starts a new one.
                if (i + 1 < graph.size()) {
                    is_leader[i + 2] = true;  // next node is a leader
                }
                break;
            default:
                break;
        }
    }

    // Second pass: create blocks.
    BasicBlock* current = nullptr;
    for (std::size_t i = 0; i < graph.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = graph.node(id);
        if (n.is_dead()) continue;

        if (is_leader[id.value] || current == nullptr) {
            BasicBlock bb;
            bb.id = static_cast<uint32_t>(bs.blocks.size());
            bb.leader = id;
            bs.blocks.push_back(bb);
            current = &bs.blocks.back();
        }
        current->last = id;
        bs.node_to_block[id.value] = current->id;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 2: Connect control-flow edges.
//
//   For each block, determine its successors based on the last node:
//   - If → two successors: the block containing IfTrue, and the block
//     containing IfFalse (identified by ctrl_input).
//   - Jump → one successor: the block after Jump (fall-through, or the
//     target if we had explicit jump targets).
//   - Return / Throw → no successors (terminal).
//   - Other → one successor: the next block (fall-through).
// ─────────────────────────────────────────────────────────────────────────────

void BuildRegionsPass::connect_edges(const Graph& graph, BlockStructure& bs) {
    for (auto& bb : bs.blocks) {
        if (!bb.last.valid()) continue;
        const Node& last_node = graph.node(bb.last);

        switch (last_node.kind) {
            case NodeKind::If: {
                // Find the IfTrue and IfFalse blocks that reference this If.
                for (std::size_t j = 0; j < graph.size(); ++j) {
                    const NodeId other{static_cast<uint32_t>(j + 1)};
                    const Node& on = graph.node(other);
                    if (on.is_dead()) continue;
                    if (graph.ctrl_input(other) == bb.last) {
                        if (on.kind == NodeKind::IfTrue || on.kind == NodeKind::IfFalse) {
                            uint32_t target_block = bs.block_of(other);
                            if (target_block < bs.blocks.size()) {
                                bb.successors.push_back(target_block);
                            }
                        }
                    }
                }
                break;
            }
            case NodeKind::Jump: {
                // Back-edge: the Jump targets the nearest Loop header
                // (a node with kind Loop that appears earlier in the graph).
                // Find the Loop header block.
                for (std::size_t j = 0; j < graph.size(); ++j) {
                    const NodeId other{static_cast<uint32_t>(j + 1)};
                    const Node& on = graph.node(other);
                    if (on.is_dead()) continue;
                    if (on.kind == NodeKind::Loop) {
                        uint32_t target_block = bs.block_of(other);
                        if (target_block < bs.blocks.size()) {
                            bb.successors.push_back(target_block);
                        }
                        break;   // first Loop header found
                    }
                }
                break;
            }
            case NodeKind::Return:
            case NodeKind::Throw:
                // Terminal — no successors.
                break;
            default:
                // Fall-through to the next block.
                if (bb.id + 1 < bs.blocks.size()) {
                    bb.successors.push_back(bb.id + 1);
                }
                break;
        }
    }

    // Build predecessor lists from successor lists.
    for (auto& bb : bs.blocks) {
        for (uint32_t succ : bb.successors) {
            if (succ < bs.blocks.size()) {
                bs.blocks[succ].predecessors.push_back(bb.id);
            }
        }
    }

    // Mark merge blocks (≥2 predecessors).
    for (auto& bb : bs.blocks) {
        if (bb.predecessors.size() >= 2) {
            bb.is_merge = true;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3: Detect loops.
//
//   A back-edge is an edge from block B to block A where A ≤ B (in block
//   ID order). The target A is a loop header.
//
//   This is a simplified version: we check for edges where target ≤ source.
//   A full implementation would use the dominator tree to verify that A
//   dominates B (which is the definition of a natural loop back-edge).
// ─────────────────────────────────────────────────────────────────────────────

void BuildRegionsPass::detect_loops(BlockStructure& bs) {
    for (auto& bb : bs.blocks) {
        for (uint32_t succ : bb.successors) {
            if (succ <= bb.id) {
                // Back-edge: succ is a loop header.
                if (succ < bs.blocks.size()) {
                    bs.blocks[succ].is_loop_header = true;
                    // The pre-header is the block just before the loop header.
                    if (succ > 0) {
                        bs.blocks[succ].loop_pre_header = succ - 1;
                    }
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 4: Compute dominator tree (Cooper-Harvey-Kennedy iterative).
//
//   Reference: Cooper, Harvey, Kennedy, "A Simple, Fast Dominance Algorithm"
//   (Rice University TR, 2001).
//
//   For each block B, find its immediate dominator IDom(B) — the closest
//   block that dominates B. Block 0 (Start) dominates everything.
//
//   Algorithm:
//     1. Initialize IDom(0) = 0, IDom(others) = undefined.
//     2. Process blocks in reverse post-order.
//     3. For each block B with processed predecessors, compute:
//          new_idom = first processed predecessor
//          for each other processed predecessor P:
//              new_idom = intersect(P, new_idom)
//          if new_idom != IDom(B), set IDom(B) = new_idom, changed.
//     4. Repeat until no change.
//
//   intersect(A, B) walks up the dominator chain until they meet.
// ─────────────────────────────────────────────────────────────────────────────

void BuildRegionsPass::compute_dominators(BlockStructure& bs) {
    if (bs.blocks.empty()) return;

    // Initialize.
    for (auto& bb : bs.blocks) {
        bb.immediate_dominator = 0;  // 0 = undefined (will be set to self for root)
    }
    bs.blocks[0].immediate_dominator = 0;  // root dominates itself

    // Simple iterative fixpoint (no RPO optimization for simplicity).
    bool changed = true;
    int iterations = 0;
    while (changed && iterations < 1000) {
        changed = false;
        ++iterations;

        for (std::size_t i = 1; i < bs.blocks.size(); ++i) {
            BasicBlock& bb = bs.blocks[i];
            if (bb.predecessors.empty()) continue;

            // Find first processed predecessor (one with IDom != undefined).
            uint32_t new_idom = 0;
            for (uint32_t pred : bb.predecessors) {
                if (pred < bs.blocks.size() && bs.blocks[pred].immediate_dominator != 0) {
                    new_idom = pred;
                    break;
                }
            }
            if (new_idom == 0) continue;

            // Intersect with other processed predecessors.
            for (uint32_t pred : bb.predecessors) {
                if (pred == new_idom) continue;
                if (pred >= bs.blocks.size()) continue;
                if (bs.blocks[pred].immediate_dominator == 0) continue;

                // intersect(pred, new_idom)
                uint32_t a = pred;
                uint32_t b = new_idom;
                while (a != b) {
                    while (a > b && a > 0 && bs.blocks[a].immediate_dominator != 0) {
                        a = bs.blocks[a].immediate_dominator;
                    }
                    while (b > a && b > 0 && bs.blocks[b].immediate_dominator != 0) {
                        b = bs.blocks[b].immediate_dominator;
                    }
                    if (a == 0 || b == 0) break;
                }
                new_idom = a;
            }

            if (bb.immediate_dominator != new_idom) {
                bb.immediate_dominator = new_idom;
                changed = true;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Top-level driver
// ─────────────────────────────────────────────────────────────────────────────

BlockStructure BuildRegionsPass::run(const Graph& graph) {
    BlockStructure bs;
    identify_blocks(graph, bs);
    connect_edges(graph, bs);
    detect_loops(bs);
    compute_dominators(bs);
    return bs;
}

BlockStructure build_block_structure(const Graph& graph) {
    BuildRegionsPass pass;
    return pass.run(graph);
}

// ─────────────────────────────────────────────────────────────────────────────
// BlockStructure::reverse_post_order
//
//   DFS from block 0, skip back-edges, then reverse the post-order.
//   This is the canonical "RPO" traversal used by every block-scheduled
//   emitter and dominator algorithm.
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint32_t> BlockStructure::reverse_post_order() const {
    if (blocks.empty()) return {};

    std::vector<uint32_t> post_order;
    post_order.reserve(blocks.size());
    std::vector<bool> visited(blocks.size(), false);
    std::vector<bool> on_stack(blocks.size(), false);

    // Iterative DFS to avoid stack overflow on large graphs.
    struct Frame { uint32_t block; uint32_t succ_idx; };
    std::vector<Frame> stack;
    stack.reserve(blocks.size());
    stack.push_back({0, 0});
    visited[0] = true;
    on_stack[0] = true;

    while (!stack.empty()) {
        Frame& top = stack.back();
        if (top.succ_idx >= blocks[top.block].successors.size()) {
            // All successors processed — post-order visit.
            post_order.push_back(top.block);
            on_stack[top.block] = false;
            stack.pop_back();
            continue;
        }
        uint32_t succ = blocks[top.block].successors[top.succ_idx++];
        if (succ >= blocks.size()) continue;
        if (visited[succ]) {
            // Already visited. If on_stack, it's a back-edge — skip.
            // Otherwise it's a cross/forward edge — already in post-order.
            continue;
        }
        visited[succ] = true;
        on_stack[succ] = true;
        stack.push_back({succ, 0});
    }

    // Reverse post-order = post-order reversed.
    std::reverse(post_order.begin(), post_order.end());
    return post_order;
}

// ─────────────────────────────────────────────────────────────────────────────
// BlockStructure::node_ids_in_block
//
//   Walks [leader.value, last.value] and emits all live NodeIds.
//   Assumes BuildRegions stored leader/last as NodeId.value ranges
//   (which it does, because identify_blocks walks nodes in NodeId order).
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint32_t> BlockStructure::node_ids_in_block(const Graph& g, uint32_t block_id) const {
    if (block_id >= blocks.size()) return {};
    const BasicBlock& bb = blocks[block_id];
    if (!bb.leader.valid() || !bb.last.valid()) return {};

    std::vector<uint32_t> out;
    out.reserve(bb.last.value - bb.leader.value + 1);
    for (uint32_t v = bb.leader.value; v <= bb.last.value; ++v) {
        if (v == 0 || v > g.size()) continue;
        NodeId id{v};
        if (g.node(id).is_dead()) continue;
        // Verify this node actually belongs to this block (safety check).
        auto it = node_to_block.find(v);
        if (it == node_to_block.end() || it->second != block_id) continue;
        out.push_back(v);
    }
    return out;
}

}  // namespace jade
