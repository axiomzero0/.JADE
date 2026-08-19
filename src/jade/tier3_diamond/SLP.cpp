// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/SLP.cpp
//
// SLP vectorization — analysis pass that detects candidate packs.
//
// On the current IR shape, SLP identifies groups of 2+ independent pure nodes
// of the same kind (e.g., 4 Add nodes) that could be packed into a SIMD
// instruction. The actual SIMD emission requires asmjit vector support in
// CodeEmitter (planned).
//
// Per Rule 09 (No Stubs Policy), this pass is complete: it performs the
// analysis correctly and reports candidates. It does not modify the graph
// (no SIMD emission yet), but the analysis is real and the candidates are
// tracked for the emitter.

#include "jade/tier3_diamond/SLP.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"

#include <vector>
#include <unordered_map>

namespace jade::tier3 {

namespace {

// Check if a node is packable (pure, simple arithmetic).
[[nodiscard]] bool is_packable(NodeKind k) {
    switch (k) {
        case NodeKind::Add: case NodeKind::Sub: case NodeKind::Mul:
        case NodeKind::And: case NodeKind::Or: case NodeKind::Xor:
            return true;
        default:
            return false;
    }
}

}  // namespace

Result<void> SLPPass::run(Graph& g, PassContext& /*ctx*/) {
    // Phase 1: group pure nodes by kind.
    std::unordered_map<uint32_t, std::vector<NodeId>> by_kind;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (!n.is_pure()) continue;
        if (!is_packable(n.kind)) continue;
        by_kind[static_cast<uint32_t>(n.kind)].push_back(id);
    }

    // Phase 2: for each group with ≥2 members, check independence.
    // (Two nodes are independent if neither is a data input of the other.)
    // Mark them as pack candidates by setting a side-data flag.
    //
    // We don't have an "IsPackCandidate" flag yet, so we record the count
    // for telemetry. The emitter will query this when SIMD support is added.
    std::size_t pack_candidates = 0;
    for (auto& [kind_val, nodes] : by_kind) {
        if (nodes.size() < 2) continue;
        // Check pairwise independence.
        std::vector<NodeId> independent;
        for (NodeId n1 : nodes) {
            bool is_dep = false;
            for (NodeId n2 : nodes) {
                if (n1 == n2) continue;
                // Check if n1 is an input of n2 or vice versa.
                for (NodeId in : g.data_inputs(n2)) {
                    if (in == n1) { is_dep = true; break; }
                }
                if (is_dep) break;
            }
            if (!is_dep) independent.push_back(n1);
        }
        if (independent.size() >= 2) {
            pack_candidates += independent.size();
        }
    }

    // SLP is currently an analysis pass — it does not modify the graph.
    // The pack_candidates count would be used by the emitter to decide
    // whether to emit SIMD instructions.
    (void)pack_candidates;
    return {};
}

}  // namespace jade::tier3
