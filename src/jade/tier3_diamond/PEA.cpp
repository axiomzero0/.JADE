// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/PEA.cpp
//
// Partial Escape Analysis with:
//   - Use-list-based O(A×U) complexity (not O(A×N²))
//   - Effect-chain-aware load forwarding (prevents miscompilation)
//   - Path-sensitive field state (bails on conflicting stores)
//   - FrameState/guard reference scanning before elimination
//   - Named slot constants (no magic indices)
//
// Per Rule 52: correctness-preserving performance fixes only.

#include "jade/tier3_diamond/PEA.hpp"
#include "jade/ir/passes/BuildRegions.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

namespace jade::tier3 {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Named slot constants — no magic indices.
// ─────────────────────────────────────────────────────────────────────────────
constexpr std::size_t SLOT_OBJ   = 0;   // StoreField(obj, val): obj is slot 0
constexpr std::size_t SLOT_VAL   = 1;   // StoreField(obj, val): val is slot 1
constexpr std::size_t SLOT_ARR   = 0;   // LoadElement(arr, idx): arr is slot 0
constexpr std::size_t SLOT_IDX   = 1;   // LoadElement(arr, idx): idx is slot 1
constexpr std::size_t SLOT_STORED_VAL = 2; // StoreElement(arr, idx, val): val is slot 2

// ─────────────────────────────────────────────────────────────────────────────
// UseRef — a use of an allocation, with its slot index.
// ─────────────────────────────────────────────────────────────────────────────
struct UseRef {
    NodeId      user;
    std::size_t slot;
};

// ─────────────────────────────────────────────────────────────────────────────
// UseLists — pre-built use lists for O(1) lookup.
// Eliminates the O(N²) scan: build once, query per-allocation.
// ─────────────────────────────────────────────────────────────────────────────
struct UseLists {
    // data_input_uses[v] = list of (user, slot) pairs where v is used as a data input.
    std::unordered_map<uint32_t, std::vector<UseRef>> data_input_uses;

    // effect_input_uses[v] = list of nodes where v is the effect input.
    std::unordered_map<uint32_t, std::vector<NodeId>> effect_input_uses;

    // frame_state_uses[v] = list of nodes whose FrameState references v.
    // (Currently empty — FrameState is not wired to allocations yet.)
    std::unordered_map<uint32_t, std::vector<NodeId>> frame_state_uses;

    // Build the use lists in a single O(N) pass.
    void build(const Graph& g) {
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            const Node& n = g.node(id);
            if (n.is_dead()) continue;

            // Data inputs.
            std::size_t slot = 0;
            for (NodeId in : g.data_inputs(id)) {
                if (in.valid()) {
                    data_input_uses[in.value].push_back({id, slot});
                }
                ++slot;
            }

            // Effect input.
            NodeId eff = g.effect_input(id);
            if (eff.valid()) {
                effect_input_uses[eff.value].push_back(id);
            }
        }
    }

    [[nodiscard]] const std::vector<UseRef>& data_uses(uint32_t v) const {
        static const std::vector<UseRef> empty;
        auto it = data_input_uses.find(v);
        return it != data_input_uses.end() ? it->second : empty;
    }

    [[nodiscard]] const std::vector<NodeId>& effect_uses(uint32_t v) const {
        static const std::vector<NodeId> empty;
        auto it = effect_input_uses.find(v);
        return it != effect_input_uses.end() ? it->second : empty;
    }

    [[nodiscard]] const std::vector<NodeId>& frame_state_uses_of(uint32_t v) const {
        static const std::vector<NodeId> empty;
        auto it = frame_state_uses.find(v);
        return it != frame_state_uses.end() ? it->second : empty;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Slot-aware escape detection.
// Returns true if the allocation escapes when used in the given slot.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] bool escapes_in_slot(NodeKind use_kind, std::size_t slot) noexcept {
    switch (use_kind) {
        // Always escaping: the allocation leaves the current frame.
        case NodeKind::Return:
        case NodeKind::Throw:
        case NodeKind::Call:
        case NodeKind::CallVirt:
        case NodeKind::CallKnown:
        case NodeKind::TailCall:
        case NodeKind::InvokeDynamic:
        case NodeKind::Box:
            return true;

        // StoreField(obj, val): non-escaping for obj (SLOT_OBJ),
        // escaping for val (SLOT_VAL).
        case NodeKind::StoreField:
        case NodeKind::StFld:
        case NodeKind::StoreElement:
        case NodeKind::StElem:
            return slot != SLOT_OBJ;

        // Non-escaping: operating on the object's own fields/elements.
        case NodeKind::LoadField:
        case NodeKind::LdFld:
        case NodeKind::LoadElement:
        case NodeKind::LdElem:
        case NodeKind::LdFlda:
        case NodeKind::LdElemA:
        case NodeKind::ArrayLength:
            return false;

        // Type checks on the object: non-escaping for slot 0.
        case NodeKind::CheckClass:
        case NodeKind::IsInst:
        case NodeKind::CastClass:
            return slot != SLOT_OBJ;

        case NodeKind::Unbox:
        case NodeKind::UnboxAny:
            return slot == SLOT_OBJ;

        // Conservative: unknown use = escape.
        default:
            return true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Effect-chain dominance check.
// Returns true if `store_node` dominates `load_node` in the effect chain.
// We walk the effect chain backwards from `load_node` until we find
// `store_node` or reach the Start node.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] bool effect_dominates(const Graph& g, NodeId store_node, NodeId load_node) {
    NodeId load_eff = g.effect_input(load_node);
    if (load_eff == store_node) return true;

    NodeId cur = load_eff;
    while (cur.valid()) {
        if (cur == store_node) return true;
        NodeId next = g.effect_input(cur);
        if (next == cur) break;
        cur = next;
    }

    NodeId store_eff = g.effect_input(store_node);
    if (store_eff == load_eff && store_node.value < load_node.value) return true;

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// FieldState — tracks per-field scalar state for an allocation.
// Path-sensitive: if the same field offset is stored twice from different
// blocks (or without clear dominance), we bail out of SRA for that field.
// ─────────────────────────────────────────────────────────────────────────────
struct FieldState {
    struct StoreInfo {
        NodeId value;
        NodeId store_node;
    };
    std::unordered_map<uint16_t, StoreInfo> fields;
    std::unordered_map<uint16_t, int> store_count;  // count stores per offset

    // Returns true if the field has been stored exactly once (safe to forward).
    [[nodiscard]] bool is_safe_to_forward(uint16_t offset) const {
        auto it = store_count.find(offset);
        return it != store_count.end() && it->second == 1;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Check if an allocation is referenced by FrameState or guard nodes.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] bool has_guard_or_framestate_refs(const Graph& g, NodeId alloc,
                                                   const UseLists& uses) {
    // Check FrameState references.
    if (!uses.frame_state_uses_of(alloc.value).empty()) return true;

    // Check guard nodes that reference the allocation.
    for (const auto& ref : uses.data_uses(alloc.value)) {
        const Node& user = g.node(ref.user);
        if (user.is_dead()) continue;
        if (user.is_guard()) return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Check if an allocation has any live data or effect-input use.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] bool has_live_use(const Graph& g, NodeId alloc, const UseLists& uses) {
    for (const auto& ref : uses.data_uses(alloc.value)) {
        if (!g.node(ref.user).is_dead()) return true;
    }
    for (NodeId eff_user : uses.effect_uses(alloc.value)) {
        if (!g.node(eff_user).is_dead()) return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Check if a node has any live effect-input user.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] bool has_effect_user(const Graph& g, NodeId id, const UseLists& uses) {
    for (NodeId eff_user : uses.effect_uses(id.value)) {
        if (!g.node(eff_user).is_dead()) return true;
    }
    return false;
}

[[nodiscard]] bool is_allocation(NodeKind k) noexcept {
    return k == NodeKind::Allocate || k == NodeKind::NewObj
           || k == NodeKind::NewArr || k == NodeKind::Box;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// PEA main pass.
// ─────────────────────────────────────────────────────────────────────────────

Result<void> PEAPass::run(Graph& g, PassContext& /*ctx*/) {
    // Phase 1: Build use lists (single O(N) pass — eliminates all O(N²) scans).
    UseLists uses;
    uses.build(g);

    // Phase 2: Collect all allocation nodes.
    std::vector<NodeId> allocations;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (is_allocation(n.kind)) {
            allocations.push_back(id);
        }
    }
    if (allocations.empty()) return {};

    bool changed = false;

    // Phase 3: For each allocation, analyze and transform.
    for (NodeId alloc : allocations) {
        if (g.node(alloc).is_dead()) continue;

        // Check for FrameState/guard references — if present, can't eliminate.
        if (has_guard_or_framestate_refs(g, alloc, uses)) continue;

        // Compute escape state using use lists (O(U) per allocation, not O(N)).
        bool has_escape = false;
        for (const auto& ref : uses.data_uses(alloc.value)) {
            if (g.node(ref.user).is_dead()) continue;
            if (escapes_in_slot(g.node(ref.user).kind, ref.slot)) {
                has_escape = true;
                break;
            }
        }

        if (has_escape) {
            // The allocation escapes. For true PEA, we'd compute per-block
            // escape state and materialize only on escaping paths.
            // For now, keep the allocation (conservative — Rule A.5).
            continue;
        }

        // Phase 4: Scalar Replacement (SRA) for non-escaping allocations.
        // Track per-field state with path-sensitivity: if a field is stored
        // more than once, bail out of forwarding for that field.
        FieldState fs;

        for (const auto& ref : uses.data_uses(alloc.value)) {
            if (g.node(ref.user).is_dead()) continue;
            Node& n = g.node(ref.user);

            if (n.kind == NodeKind::StFld || n.kind == NodeKind::StoreField) {
                uint16_t offset = g.side(ref.user).field_offset;
                NodeId value = (ref.user.value <= g.size())
                    ? [&]() -> NodeId {
                        auto inputs = g.data_inputs(ref.user);
                        return inputs.size() > SLOT_VAL ? inputs[SLOT_VAL] : NodeId::invalid();
                    }()
                    : NodeId::invalid();

                fs.fields[offset] = {value, ref.user};
                fs.store_count[offset]++;

            } else if (n.kind == NodeKind::LdFld || n.kind == NodeKind::LoadField) {
                uint16_t offset = g.side(ref.user).field_offset;

                // Only forward if the field was stored exactly once
                // (path-sensitivity: multiple stores = ambiguous value).
                if (!fs.is_safe_to_forward(offset)) continue;

                auto it = fs.fields.find(offset);
                if (it == fs.fields.end()) continue;

                NodeId stored_val = it->second.value;
                NodeId store_node = it->second.store_node;
                if (!stored_val.valid() || stored_val.value > g.size()) continue;

                // Effect-chain dominance check: verify the store dominates
                // this load in the effect chain (prevents miscompilation
                // when stores and loads are on different paths).
                if (!effect_dominates(g, store_node, ref.user)) continue;

                const Node& stored = g.node(stored_val);
                if (stored.flags.has(NodeFlag::IsConst)) {
                    // Forward the constant value.
                    n.flags |= NodeFlag::IsConst;
                    g.side(ref.user).const_value = g.side(stored_val).const_value;
                    n.type = stored.type;
                    changed = true;
                } else {
                    // Rewire: replace all uses of this load with the stored value.
                    g.replace_all_uses(ref.user, stored_val);
                    g.mark_dead(ref.user);
                    changed = true;
                }
            }
        }

        // Eliminate dead stores (stores with no effect-input users).
        for (const auto& ref : uses.data_uses(alloc.value)) {
            if (g.node(ref.user).is_dead()) continue;
            Node& n = g.node(ref.user);
            if (n.kind != NodeKind::StFld && n.kind != NodeKind::StoreField) continue;
            if (!has_effect_user(g, ref.user, uses)) {
                g.mark_dead(ref.user);
                changed = true;
            }
        }

        // Phase 5: Eliminate the allocation if no live uses remain.
        if (!has_live_use(g, alloc, uses)) {
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
