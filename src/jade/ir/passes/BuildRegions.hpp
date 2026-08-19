// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/BuildRegions.hpp
//
// The BuildRegions pass is the single highest-ROI pass in .JADE.
// It identifies basic blocks, inserts Region nodes at merge points,
// detects back-edges and Loop nodes, and computes the dominator tree.
//
// Without this pass, LICM, GCM, LoopUnrolling, LoopPeeling,
// LoopUnswitching, PEA-materialization, and SRA-with-Phi are all
// permanently dead no-ops.
//
// Algorithm:
//   1. Walk all nodes in NodeId order.
//   2. A "block leader" is: Start, a node with a ctrl input from If/Region/Loop,
//      IfTrue, IfFalse, Jump.
//   3. Group consecutive non-leader nodes into basic blocks.
//   4. Identify merge points (blocks with ≥2 predecessors).
//   5. Detect back-edges (a control edge from block B to block A where A ≤ B).
//   6. Compute the dominator tree (Cooper-Harvey-Kennedy iterative).
//
// The pass does NOT modify the graph — it produces a BlockStructure
// side-table that other passes query.

#pragma once

#include "jade/core/NodeId.hpp"
#include "jade/ir/Graph.hpp"

#include <cstdint>
#include <vector>
#include <unordered_map>

namespace jade {

// ─────────────────────────────────────────────────────────────────────────────
// BasicBlock — a maximal sequence of nodes with no internal control flow.
// ─────────────────────────────────────────────────────────────────────────────
struct BasicBlock {
    uint32_t id{0};
    NodeId   leader{NodeId::invalid()};      // first node in the block
    NodeId   last{NodeId::invalid()};          // last node in the block
    std::vector<uint32_t> predecessors;        // block IDs of predecessors
    std::vector<uint32_t> successors;          // block IDs of successors
    bool     is_loop_header{false};
    bool     is_merge{false};                  // ≥2 predecessors
    uint32_t loop_pre_header{0};               // block ID of the pre-header (if loop header)

    // Dominator info (filled by ComputeDominators)
    uint32_t immediate_dominator{0};            // 0 = no dominator (root)
};

// ─────────────────────────────────────────────────────────────────────────────
// BlockStructure — the output of BuildRegions.
// ─────────────────────────────────────────────────────────────────────────────
struct BlockStructure {
    std::vector<BasicBlock> blocks;
    std::unordered_map<uint32_t, uint32_t> node_to_block;  // NodeId.value → block ID

    [[nodiscard]] uint32_t block_of(NodeId id) const {
        auto it = node_to_block.find(id.value);
        return it != node_to_block.end() ? it->second : 0;
    }

    [[nodiscard]] bool dominates(uint32_t a, uint32_t b) const {
        // Walk the dominator chain from b up to a.
        uint32_t cur = b;
        while (cur != 0) {
            if (cur == a) return true;
            if (cur >= blocks.size()) return false;
            cur = blocks[cur].immediate_dominator;
            if (cur == blocks[cur].immediate_dominator) break;  // root
        }
        return a == 0;
    }

    [[nodiscard]] uint32_t num_blocks() const noexcept { return blocks.size(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// BuildRegionsPass — computes the block structure.
// ─────────────────────────────────────────────────────────────────────────────
class BuildRegionsPass {
public:
    [[nodiscard]] BlockStructure run(const Graph& graph);

private:
    void identify_blocks(const Graph& graph, BlockStructure& bs);
    void connect_edges(const Graph& graph, BlockStructure& bs);
    void detect_loops(BlockStructure& bs);
    void compute_dominators(BlockStructure& bs);
};

// Convenience: run BuildRegions and return the structure.
[[nodiscard]] BlockStructure build_block_structure(const Graph& graph);

}  // namespace jade
