// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/SRA.cpp
//
// Scalar Replacement of Aggregates — works with PEA.
//
// For each non-escaping allocation (determined by PEA's escape analysis):
//   1. Track StoreField(alloc, offset, val) → field[offset] = val.
//   2. Replace LoadField(alloc, offset) with field[offset] (forward).
//   3. Eliminate dead stores.
//   4. Eliminate the allocation if no live uses remain.
//
// Slot-aware escape detection: StoreField(obj, val) is non-escaping for
// `obj` (storing INTO the object) but escaping for `val` (the value
// being stored might be another allocation).

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

// Slot-aware escape check: returns true if the allocation escapes when
// used in the given slot of the given node kind.
[[nodiscard]] bool escapes_in_slot(NodeKind use_kind, std::size_t slot) {
    switch (use_kind) {
        case NodeKind::Return:
        case NodeKind::Throw:
        case NodeKind::Call:
        case NodeKind::CallVirt:
        case NodeKind::CallKnown:
        case NodeKind::TailCall:
        case NodeKind::InvokeDynamic:
        case NodeKind::Box:
            return true;
        case NodeKind::StoreField:
        case NodeKind::StFld:
        case NodeKind::StoreElement:
        case NodeKind::StElem:
            return slot != 0;  // escaping only if it's the value
        case NodeKind::LoadField:
        case NodeKind::LdFld:
        case NodeKind::LoadElement:
        case NodeKind::LdElem:
        case NodeKind::LdFlda:
        case NodeKind::LdElemA:
        case NodeKind::ArrayLength:
            return false;
        case NodeKind::CheckClass:
        case NodeKind::IsInst:
        case NodeKind::CastClass:
            return slot != 0;
        case NodeKind::Unbox:
        case NodeKind::UnboxAny:
            return slot == 0;
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

        // Check if this allocation escapes (slot-aware).
        bool escapes = false;
        for (std::size_t j = 0; j < g.size(); ++j) {
            const NodeId other_id{static_cast<uint32_t>(j + 1)};
            if (other_id == id) continue;
            const Node& other = g.node(other_id);
            if (other.is_dead()) continue;
            auto other_inputs = g.data_inputs(other_id);
            for (std::size_t slot = 0; slot < other_inputs.size(); ++slot) {
                if (other_inputs[slot] == id && escapes_in_slot(other.kind, slot)) {
                    escapes = true;
                    break;
                }
            }
            if (escapes) break;
        }
        if (!escapes) candidates.push_back(id);
    }
    if (candidates.empty()) return {};

    bool changed = false;

    // For each candidate, track StoreField → LoadField forwarding.
    for (NodeId alloc : candidates) {
        std::unordered_map<uint16_t, NodeId> last_store;

        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            Node& n = g.node(id);
            if (n.is_dead()) continue;

            auto inputs = g.data_inputs(id);
            if (inputs.empty()) continue;
            NodeId obj = inputs[0];
            if (obj != alloc) continue;

            if (n.kind == NodeKind::StFld || n.kind == NodeKind::StoreField) {
                uint16_t offset = g.side(id).field_offset;
                NodeId value = inputs.size() >= 2 ? inputs[1] : NodeId::invalid();
                last_store[offset] = value;

                // Mark store dead if no effect-input user.
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
                    NodeId stored_val = it->second;
                    if (stored_val.valid() && stored_val.value <= g.size()) {
                        const Node& stored = g.node(stored_val);
                        if (stored.flags.has(NodeFlag::IsConst)) {
                            n.flags |= NodeFlag::IsConst;
                            g.side(id).const_value = g.side(stored_val).const_value;
                            n.type = stored.type;
                            changed = true;
                        } else {
                            g.replace_all_uses(id, stored_val);
                            g.mark_dead(id);
                            changed = true;
                        }
                    }
                }
            }
        }

        // Eliminate allocation if no live uses.
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
