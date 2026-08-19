// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/SRA.cpp
//
// Scalar Replacement of Aggregates.
//
// For each non-escaping Allocate/NewObj:
//   1. Replace each LoadField(alloc, offset) with the last value stored
//      to that offset (StoreField tracking).
//   2. Eliminate the StoreField and the Allocate.
//
// On the current linear IR (no Phi insertion), this works for straight-line
// code where the store dominates the load. For loops and merges, we need
// Phi nodes per field, which requires block structure.

#include "jade/tier3_diamond/SRA.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jade::tier3 {

namespace {

[[nodiscard]] bool is_allocation(NodeKind k) {
    return k == NodeKind::Allocate || k == NodeKind::NewObj
           || k == NodeKind::NewArr || k == NodeKind::Box;
}

[[nodiscard]] bool is_escape_use(NodeKind k) {
    switch (k) {
        case NodeKind::Return: case NodeKind::Throw:
        case NodeKind::Call: case NodeKind::CallVirt:
        case NodeKind::CallKnown: case NodeKind::TailCall:
        case NodeKind::StoreField: case NodeKind::StoreElement:
        case NodeKind::StFld: case NodeKind::StElem:
            return true;
        case NodeKind::LoadField: case NodeKind::LdFld:
        case NodeKind::LdElem: case NodeKind::LdFlda:
        case NodeKind::LdElemA: case NodeKind::ArrayLength:
            return false;
        default:
            return true;
    }
}

}  // namespace

Result<void> SRAPass::run(Graph& g, PassContext& /*ctx*/) {
    // Find non-escaping allocations with field accesses.
    std::vector<NodeId> candidates;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (!is_allocation(n.kind)) continue;

        // Check if this allocation escapes. An allocation escapes if it is
        // used as a VALUE (not as the object being operated on). For
        // StoreField(obj, value), the alloc escapes only if it's `value`.
        bool escapes = false;
        for (std::size_t j = 0; j < g.size(); ++j) {
            const NodeId other_id{static_cast<uint32_t>(j + 1)};
            if (other_id == id) continue;
            const Node& other = g.node(other_id);
            if (other.is_dead()) continue;
            auto other_inputs = g.data_inputs(other_id);
            for (std::size_t slot = 0; slot < other_inputs.size(); ++slot) {
                if (other_inputs[slot] != id) continue;
                // The alloc is used in slot `slot` of `other`.
                // For StoreField/StFld: slot 0 = obj (non-escaping), slot 1 = value (escaping).
                // For LdFld/LdElem: slot 0 = obj (non-escaping).
                // For Call/Return/Throw: any slot = escaping.
                if (other.kind == NodeKind::StoreField
                    || other.kind == NodeKind::StFld
                    || other.kind == NodeKind::StoreElement
                    || other.kind == NodeKind::StElem) {
                    if (slot == 0) continue;   // alloc is the object — non-escaping
                    escapes = true;
                    break;
                }
                if (other.kind == NodeKind::LoadField
                    || other.kind == NodeKind::LdFld
                    || other.kind == NodeKind::LdElem
                    || other.kind == NodeKind::LdFlda
                    || other.kind == NodeKind::LdElemA
                    || other.kind == NodeKind::ArrayLength) {
                    continue;   // alloc is the object — non-escaping
                }
                // Any other use = escaping.
                escapes = true;
                break;
            }
            if (escapes) break;
        }
        if (escapes) continue;
        candidates.push_back(id);
    }
    if (candidates.empty()) return {};

    bool changed = false;

    // For each candidate, track StoreField → LoadField forwarding.
    // field_offset → last stored NodeId
    for (NodeId alloc : candidates) {
        std::unordered_map<uint16_t, NodeId> last_store;

        // Walk all nodes in order. Forward loads to the last store.
        // Eliminate stores at the end (they're dead — no load reads them).
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            Node& n = g.node(id);
            if (n.is_dead()) continue;

            // Check if this node operates on `alloc`.
            auto inputs = g.data_inputs(id);
            if (inputs.empty()) continue;
            NodeId obj = inputs[0];
            if (obj != alloc) continue;

            if (n.kind == NodeKind::StFld || n.kind == NodeKind::StoreField) {
                // Record the store: offset → value.
                uint16_t offset = g.side(id).field_offset;
                NodeId value = inputs.size() >= 2 ? inputs[1] : NodeId::invalid();
                last_store[offset] = value;
                // Only mark the store dead if no other node references it
                // via effect input. (LoadField's effect input may point to
                // this store — we can't mark it dead without rewiring.)
                bool has_effect_user = false;
                for (std::size_t j = 0; j < g.size(); ++j) {
                    const NodeId other_id{static_cast<uint32_t>(j + 1)};
                    if (other_id == id) continue;
                    const Node& other = g.node(other_id);
                    if (other.is_dead()) continue;
                    if (g.effect_input(other_id) == id) { has_effect_user = true; break; }
                }
                if (!has_effect_user) {
                    g.mark_dead(id);
                    changed = true;
                }
            } else if (n.kind == NodeKind::LdFld || n.kind == NodeKind::LoadField) {
                uint16_t offset = g.side(id).field_offset;
                auto it = last_store.find(offset);
                if (it != last_store.end()) {
                    // Forward: the load's value is the last stored value.
                    // We can't easily rewire uses without a "replace_all_uses"
                    // API, so we mark the load as IsConst with the stored value.
                    // A better impl would rewire; this is a conservative step.
                    Node& load_node = n;
                    const NodeId stored_val = it->second;
                    if (stored_val.valid() && stored_val.value <= g.size()) {
                        const Node& stored = g.node(stored_val);
                        if (stored.flags.has(NodeFlag::IsConst)) {
                            load_node.flags |= NodeFlag::IsConst;
                            g.side(id).const_value = g.side(stored_val).const_value;
                            load_node.type = stored.type;
                            changed = true;
                        }
                    }
                }
            }
        }

        // If the allocation has no live uses after SRA, eliminate it.
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
