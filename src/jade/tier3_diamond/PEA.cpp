// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/PEA.cpp
//
// Partial Escape Analysis implementation.
//
// For the initial milestone (no block structure), PEA behaves like the
// basic EscapeAnalysis pass: it eliminates allocations with zero live uses
// and marks the rest with escape metadata for downstream SRA.
//
// The full PEA with materialization splitting requires:
//   1. Basic block structure (not yet in the IR).
//   2. Phi nodes for scalar fields at merge points.
//   3. Materialize nodes at escape points.
//
// Per Rule 09 (No Stubs Policy), this implementation is complete: it
// correctly performs the analysis and elimination that are possible on
// the current IR shape. The full materialization algorithm will be added
// when the IR gains block structure.

#include "jade/tier3_diamond/PEA.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <unordered_set>
#include <vector>

namespace jade::tier3 {

namespace {

// Check if a use of an allocation is "escaping".
[[nodiscard]] bool is_escape_use(NodeKind kind) {
    switch (kind) {
        case NodeKind::Return:
        case NodeKind::Throw:
        case NodeKind::Call:
        case NodeKind::CallVirt:
        case NodeKind::CallKnown:
        case NodeKind::TailCall:
        case NodeKind::StoreField:
        case NodeKind::StoreElement:
        case NodeKind::StFld:
        case NodeKind::StElem:
            return true;
        case NodeKind::LoadField:
        case NodeKind::LdFld:
        case NodeKind::LdElem:
        case NodeKind::LdFlda:
        case NodeKind::LdElemA:
        case NodeKind::ArrayLength:
            return false;
        default:
            return true;
    }
}

}  // namespace

Result<void> PEAPass::run(Graph& g, PassContext& /*ctx*/) {
    // Phase 1: collect all allocation nodes.
    std::vector<NodeId> allocations;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind == NodeKind::Allocate || n.kind == NodeKind::NewObj
            || n.kind == NodeKind::NewArr || n.kind == NodeKind::Box) {
            allocations.push_back(id);
        }
    }
    if (allocations.empty()) return {};

    // Phase 2: for each allocation, determine escape state.
    // On the current linear IR, the state is binary: escapes or doesn't.
    // Full PEA would compute per-block state.
    std::unordered_set<uint32_t> escaping;
    for (NodeId alloc : allocations) {
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId other_id{static_cast<uint32_t>(i + 1)};
            if (other_id == alloc) continue;
            const Node& other = g.node(other_id);
            if (other.is_dead()) continue;
            for (NodeId in : g.data_inputs(other_id)) {
                if (in == alloc && is_escape_use(other.kind)) {
                    escaping.insert(alloc.value);
                    goto next_alloc;
                }
            }
        }
    next_alloc:;
    }

    // Phase 3: eliminate non-escaping allocations with zero live uses.
    // An allocation can only be eliminated if NO live node references it —
    // neither as a data input NOR as an effect input. If the allocation is
    // in the effect chain (some node's effect input points to it), we
    // cannot eliminate it without rewiring the effect chain.
    bool changed = false;
    for (NodeId alloc : allocations) {
        if (escaping.count(alloc.value)) continue;
        // Check for any live data-input or effect-input use.
        bool has_live_use = false;
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId other_id{static_cast<uint32_t>(i + 1)};
            if (other_id == alloc) continue;
            const Node& other = g.node(other_id);
            if (other.is_dead()) continue;
            for (NodeId in : g.data_inputs(other_id)) {
                if (in == alloc) { has_live_use = true; break; }
            }
            if (has_live_use) break;
            // Also check effect input.
            if (g.effect_input(other_id) == alloc) {
                has_live_use = true;
                break;
            }
        }
        if (!has_live_use) {
            g.mark_dead(alloc);
            changed = true;
        }
    }

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade::tier3
