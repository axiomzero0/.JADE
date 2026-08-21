// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/SLP.cpp
//
// SLP (Superword-Level Parallelism) vectorization.
//
// Real implementation: detects packs of 2/4/8 isomorphic independent
// pure nodes of the same kind (e.g., 4 Add nodes) and replaces them
// with a single VectorOp node. The VectorOp has the scalar values as
// data inputs, and side_data::vector_kind records the original NodeKind.
//
// After SLP, each original scalar node is replaced by a VectorExtract
// node that extracts its lane from the VectorOp. This preserves the
// SSA semantics: uses of the original Add now read from the extract.
//
// The CodeEmitter lowers VectorOp to paddd/psubd/pmulld/pandd/pord/pxord
// (SSE2/AVX2 128/256-bit packed integer ops).

#include "jade/tier3_diamond/SLP.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <vector>
#include <unordered_map>
#include <algorithm>

namespace jade::tier3 {

namespace {

// Check if a node is packable (pure, simple arithmetic, 2 inputs).
[[nodiscard]] bool is_packable(NodeKind k) {
    switch (k) {
        case NodeKind::Add: case NodeKind::Sub: case NodeKind::Mul:
        case NodeKind::And: case NodeKind::Or: case NodeKind::Xor:
            return true;
        default:
            return false;
    }
}

// Check if two nodes are independent (neither is a data input of the other).
[[nodiscard]] bool are_independent(const Graph& g, NodeId a, NodeId b) {
    if (a == b) return false;
    // Check if a is an input of b.
    for (NodeId in : g.data_inputs(b)) {
        if (in == a) return false;
    }
    // Check if b is an input of a.
    for (NodeId in : g.data_inputs(a)) {
        if (in == b) return false;
    }
    return true;
}

}  // namespace

Result<void> SLPPass::run(Graph& g, PassContext& /*ctx*/) {
    // Phase 1: group pure packable nodes by kind.
    std::unordered_map<uint32_t, std::vector<NodeId>> by_kind;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (!n.is_pure()) continue;
        if (!is_packable(n.kind)) continue;
        // Only pack nodes with exactly 2 inputs (binary ops).
        auto inputs = g.data_inputs(id);
        if (inputs.size() != 2) continue;
        by_kind[static_cast<uint32_t>(n.kind)].push_back(id);
    }

    bool changed = false;

    // Phase 2: for each group, find packs of independent nodes and replace
    // them with VectorOp + VectorExtract nodes.
    for (auto& [kind_val, nodes] : by_kind) {
        if (nodes.size() < 2) continue;

        // Greedy packing: find the largest set of pairwise-independent nodes.
        // We try to form packs of 4 (best for SSE2 128-bit), then 2.
        // Sort by NodeId for determinism.
        std::sort(nodes.begin(), nodes.end(),
            [](NodeId a, NodeId b) { return a.value < b.value; });

        // Greedy: pick the first node, then add nodes that are independent
        // of all already-picked nodes.
        std::vector<NodeId> pack;
        for (NodeId candidate : nodes) {
            if (g.node(candidate).is_dead()) continue;
            bool independent = true;
            for (NodeId picked : pack) {
                if (!are_independent(g, candidate, picked)) {
                    independent = false;
                    break;
                }
            }
            if (independent) {
                pack.push_back(candidate);
                // Prefer packs of 4 (SSE2).
                if (pack.size() == 4) break;
            }
        }

        if (pack.size() < 2) continue;

        // We have a pack of `pack.size()` independent nodes of the same kind.
        // Create a VectorOp node with all their inputs as data inputs.
        //
        // The VectorOp takes ALL the scalar inputs from the packed nodes.
        // For a pack of 4 Add nodes, the VectorOp has 8 inputs:
        //   [add0_a, add0_b, add1_a, add1_b, add2_a, add2_b, add3_a, add3_b]
        // The emitter shuffles these into two vector registers and emits
        // a single paddd.
        std::vector<NodeId> vec_inputs;
        for (NodeId n : pack) {
            auto inputs = g.data_inputs(n);
            for (NodeId in : inputs) {
                vec_inputs.push_back(in);
            }
        }

        NodeId vec_op = g.create(NodeKind::VectorOp,
            std::span<const NodeId>{vec_inputs});
        g.side(vec_op).vector_kind = static_cast<NodeKind>(kind_val);
        g.side(vec_op).vector_lanes = static_cast<uint8_t>(pack.size());

        // Replace each packed node with a VectorExtract from the VectorOp.
        for (std::size_t lane = 0; lane < pack.size(); ++lane) {
            NodeId packed_node = pack[lane];
            if (g.node(packed_node).is_dead()) continue;

            // Create a VectorExtract node for this lane.
            NodeId extract_in[] = {vec_op};
            NodeId extract = g.create(NodeKind::VectorExtract, extract_in);
            g.side(extract).vector_lane = static_cast<uint8_t>(lane);
            g.side(extract).vector_lanes = static_cast<uint8_t>(pack.size());

            // Replace all uses of the packed node with the extract.
            g.replace_all_uses(packed_node, extract);

            // Mark the packed node dead (it's been replaced by the VectorOp).
            g.mark_dead(packed_node);
        }

        changed = true;
    }

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade::tier3
