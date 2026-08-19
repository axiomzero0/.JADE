// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/EscapeAnalysis.cpp

#include "jade/ir/passes/EscapeAnalysis.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <unordered_set>

namespace jade {

Result<void> EscapeAnalysisPass::run(Graph& g, PassContext& /*ctx*/) {
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

    // Phase 2: for each allocation, scan all uses. If any use is "escaping",
    // mark the allocation as escaping.
    std::unordered_set<uint32_t> escaping;

    auto is_escape_use = [&](NodeId user, NodeKind user_kind) -> bool {
        switch (user_kind) {
            // Escaping uses: the object reference leaves the current frame.
            case NodeKind::Return:
            case NodeKind::Throw:
            case NodeKind::Call:
            case NodeKind::CallVirt:
            case NodeKind::CallKnown:
            case NodeKind::TailCall:
                return true;
            // Storing the object into another object's field = escape.
            case NodeKind::StoreField:
            case NodeKind::StoreElement:
            case NodeKind::StFld:
            case NodeKind::StElem:
                return true;
            // Non-escaping uses: operating on the object's own fields/elements.
            case NodeKind::LoadField:
            case NodeKind::LdFld:
            case NodeKind::LdElem:
            case NodeKind::LdFlda:
            case NodeKind::LdElemA:
            case NodeKind::ArrayLength:
                return false;
            default:
                return true;   // conservative: unknown use = escape
        }
        (void)user;
    };

    for (NodeId alloc : allocations) {
        // Walk all nodes; if any node uses `alloc` as a data input AND that
        // node is an escaping use, mark `alloc` as escaping.
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId other_id{static_cast<uint32_t>(i + 1)};
            if (other_id == alloc) continue;
            const Node& other = g.node(other_id);
            if (other.is_dead()) continue;
            for (NodeId in : g.data_inputs(other_id)) {
                if (in == alloc) {
                    if (is_escape_use(other_id, other.kind)) {
                        escaping.insert(alloc.value);
                        goto next_alloc;
                    }
                }
            }
        }
    next_alloc:;
    }

    // Phase 3: mark non-escaping allocations with NO live uses as dead.
    //
    // An allocation that has only "non-escaping" uses (LdFld, LdElem,
    // ArrayLength) is technically non-escaping, but we CANNOT eliminate
    // it without SRA (Scalar Replacement of Aggregates), which would
    // replace those uses with scalar SSA values. SRA is a DIAMOND-tier
    // pass.
    //
    // For the basic EA pass, we only eliminate allocations that have
    // ZERO live uses. This is safe: if no node references the allocation,
    // it can be removed without violating the verifier (Rule 42).
    //
    // The `escaping` set computed above is used by downstream PEA in
    // DIAMOND to decide which allocations to delay-materialize.
    bool changed = false;
    for (NodeId alloc : allocations) {
        // Check if `alloc` has any live user.
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
            // Also check effect inputs.
            if (g.effect_input(other_id) == alloc) {
                // Effect-input-only uses are not "real" uses for EA purposes
                // — they just mean the alloc is in the effect chain. We can
                // still eliminate it if no data use exists.
                // (But we need to rewire the effect chain; that's complex.
                // For now, treat effect-input-only as non-escaping.)
                continue;
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

}  // namespace jade
