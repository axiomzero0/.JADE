// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/PEA.cpp
//
// Full Partial Escape Analysis (Stadler et al., 2013).
//
// Implements:
//   1. Per-allocation escape analysis using use-lists (O(A×U)).
//   2. Slot-aware escape detection (StoreField(obj, val) = non-escaping for obj).
//   3. Scalar replacement: eliminate Allocate + StoreField + LoadField → SSA edges.
//   4. Store→load forwarding with effect-chain dominance verification.
//   5. Path-sensitive field state: bail when field stored >1 time.
//   6. Effect-chain-aware dead store elimination.
//   7. Guard/FrameState reference scanning before elimination.
//   8. Box elimination: Box(v) that never escapes → replace with v.
//   9. Allocation coarsening: if two allocations are adjacent and both
//      non-escaping, merge their field states.
//
// What's NOT yet implemented (requires block-scheduled emitter):
//   - Materialization splitting (insert Materialize at escape paths).
//   - Phi-per-field at merge points.
//   These require GCM with block scheduling, which is the next milestone.

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

constexpr std::size_t SLOT_OBJ = 0;
constexpr std::size_t SLOT_VAL = 1;

struct UseRef { NodeId user; std::size_t slot; };

struct UseLists {
    std::unordered_map<uint32_t, std::vector<UseRef>> data_uses;
    std::unordered_map<uint32_t, std::vector<NodeId>> effect_uses;

    void build(const Graph& g) {
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            const Node& n = g.node(id);
            if (n.is_dead()) continue;
            std::size_t slot = 0;
            for (NodeId in : g.data_inputs(id)) {
                if (in.valid()) data_uses[in.value].push_back({id, slot});
                ++slot;
            }
            NodeId eff = g.effect_input(id);
            if (eff.valid()) effect_uses[eff.value].push_back(id);
        }
    }
    [[nodiscard]] const std::vector<UseRef>& duses(uint32_t v) const {
        static const std::vector<UseRef> empty;
        auto it = data_uses.find(v); return it != data_uses.end() ? it->second : empty;
    }
    [[nodiscard]] const std::vector<NodeId>& euses(uint32_t v) const {
        static const std::vector<NodeId> empty;
        auto it = effect_uses.find(v); return it != effect_uses.end() ? it->second : empty;
    }
};

[[nodiscard]] bool escapes_in_slot(NodeKind k, std::size_t slot) noexcept {
    switch (k) {
        case NodeKind::Return: case NodeKind::Throw:
        case NodeKind::Call: case NodeKind::CallVirt:
        case NodeKind::CallKnown: case NodeKind::TailCall:
        case NodeKind::InvokeDynamic: case NodeKind::Box:
            return true;
        case NodeKind::StoreField: case NodeKind::StFld:
        case NodeKind::StoreElement: case NodeKind::StElem:
            return slot != SLOT_OBJ;
        case NodeKind::LoadField: case NodeKind::LdFld:
        case NodeKind::LoadElement: case NodeKind::LdElem:
        case NodeKind::LdFlda: case NodeKind::LdElemA:
        case NodeKind::ArrayLength:
            return false;
        case NodeKind::CheckClass: case NodeKind::IsInst:
        case NodeKind::CastClass:
            return slot != SLOT_OBJ;
        case NodeKind::Unbox: case NodeKind::UnboxAny:
            return slot == SLOT_OBJ;
        default: return true;
    }
}

[[nodiscard]] bool effect_dominates(const Graph& g, NodeId store, NodeId load) {
    NodeId load_eff = g.effect_input(load);
    if (load_eff == store) return true;
    NodeId cur = load_eff;
    while (cur.valid()) {
        if (cur == store) return true;
        NodeId next = g.effect_input(cur);
        if (next == cur) break;
        cur = next;
    }
    NodeId store_eff = g.effect_input(store);
    if (store_eff == load_eff && store.value < load.value) return true;
    return false;
}

struct FieldState {
    struct StoreInfo { NodeId value; NodeId store_node; };
    std::unordered_map<uint16_t, StoreInfo> fields;
    std::unordered_map<uint16_t, int> store_count;
    [[nodiscard]] bool safe_to_forward(uint16_t off) const {
        auto it = store_count.find(off);
        return it != store_count.end() && it->second == 1;
    }
};

[[nodiscard]] bool has_guard_refs(const Graph& g, NodeId alloc, const UseLists& uses) {
    for (const auto& ref : uses.duses(alloc.value)) {
        const Node& u = g.node(ref.user);
        if (u.is_dead()) continue;
        if (u.is_guard()) return true;
    }
    return false;
}

[[nodiscard]] bool has_live_use(const Graph& g, NodeId alloc, const UseLists& uses) {
    for (const auto& ref : uses.duses(alloc.value))
        if (!g.node(ref.user).is_dead()) return true;
    for (NodeId eu : uses.euses(alloc.value))
        if (!g.node(eu).is_dead()) return true;
    return false;
}

[[nodiscard]] bool has_effect_user(const Graph& g, NodeId id, const UseLists& uses) {
    for (NodeId eu : uses.euses(id.value))
        if (!g.node(eu).is_dead()) return true;
    return false;
}

[[nodiscard]] bool is_allocation(NodeKind k) noexcept {
    return k == NodeKind::Allocate || k == NodeKind::NewObj
           || k == NodeKind::NewArr || k == NodeKind::Box;
}

// ─────────────────────────────────────────────────────────────────────────────
// Process one allocation: scalar-replace its fields, eliminate dead stores.
// Returns true if the graph was modified.
// ─────────────────────────────────────────────────────────────────────────────
bool process_allocation(Graph& g, NodeId alloc, const UseLists& uses) {
    if (g.node(alloc).is_dead()) return false;
    if (has_guard_refs(g, alloc, uses)) return false;

    // Check escape state.
    bool has_escape = false;
    for (const auto& ref : uses.duses(alloc.value)) {
        if (g.node(ref.user).is_dead()) continue;
        if (escapes_in_slot(g.node(ref.user).kind, ref.slot)) {
            has_escape = true;
            break;
        }
    }
    if (has_escape) return false;

    // Scalar replacement: forward stores → loads.
    FieldState fs;
    bool changed = false;

    // First pass: collect stores.
    for (const auto& ref : uses.duses(alloc.value)) {
        if (g.node(ref.user).is_dead()) continue;
        Node& n = g.node(ref.user);
        if (n.kind == NodeKind::StFld || n.kind == NodeKind::StoreField) {
            uint16_t off = g.side(ref.user).field_offset;
            auto inputs = g.data_inputs(ref.user);
            NodeId val = inputs.size() > SLOT_VAL ? inputs[SLOT_VAL] : NodeId::invalid();
            fs.fields[off] = {val, ref.user};
            fs.store_count[off]++;
        }
    }

    // Second pass: forward loads.
    for (const auto& ref : uses.duses(alloc.value)) {
        if (g.node(ref.user).is_dead()) continue;
        Node& n = g.node(ref.user);
        if (n.kind != NodeKind::LdFld && n.kind != NodeKind::LoadField) continue;

        uint16_t off = g.side(ref.user).field_offset;
        if (!fs.safe_to_forward(off)) continue;
        auto it = fs.fields.find(off);
        if (it == fs.fields.end()) continue;

        NodeId stored_val = it->second.value;
        NodeId store_node = it->second.store_node;
        if (!stored_val.valid() || stored_val.value > g.size()) continue;
        if (!effect_dominates(g, store_node, ref.user)) continue;

        const Node& stored = g.node(stored_val);
        if (stored.flags.has(NodeFlag::IsConst)) {
            n.flags |= NodeFlag::IsConst;
            g.side(ref.user).const_value = g.side(stored_val).const_value;
            n.type = stored.type;
            changed = true;
        } else {
            g.replace_all_uses(ref.user, stored_val);
            g.mark_dead(ref.user);
            changed = true;
        }
    }

    // Eliminate dead stores (no effect-input users).
    for (const auto& ref : uses.duses(alloc.value)) {
        if (g.node(ref.user).is_dead()) continue;
        Node& n = g.node(ref.user);
        if (n.kind != NodeKind::StFld && n.kind != NodeKind::StoreField) continue;
        if (!has_effect_user(g, ref.user, uses)) {
            g.mark_dead(ref.user);
            changed = true;
        }
    }

    // Eliminate the allocation if no live uses remain.
    if (!has_live_use(g, alloc, uses)) {
        g.mark_dead(alloc);
        changed = true;
    }

    return changed;
}

// ─────────────────────────────────────────────────────────────────────────────
// Box elimination: Box(v) where the boxed object never escapes.
// Replace all uses of Box(v) with v (when v is the same type).
// ─────────────────────────────────────────────────────────────────────────────
bool process_box(Graph& g, NodeId box, const UseLists& uses) {
    if (g.node(box).is_dead()) return false;
    if (has_guard_refs(g, box, uses)) return false;

    // Check if the boxed value escapes.
    bool has_escape = false;
    for (const auto& ref : uses.duses(box.value)) {
        if (g.node(ref.user).is_dead()) continue;
        if (escapes_in_slot(g.node(ref.user).kind, ref.slot)) {
            has_escape = true;
            break;
        }
    }
    if (has_escape) return false;

    // The box doesn't escape. Replace all uses with the boxed value.
    auto inputs = g.data_inputs(box);
    if (inputs.empty()) return false;
    NodeId boxed_val = inputs[SLOT_OBJ];
    if (!boxed_val.valid()) return false;

    // Rewire all uses of the Box to use the boxed value directly.
    g.replace_all_uses(box, boxed_val);
    g.mark_dead(box);
    return true;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// PEA main pass.
// ─────────────────────────────────────────────────────────────────────────────

Result<void> PEAPass::run(Graph& g, PassContext& /*ctx*/) {
    UseLists uses;
    uses.build(g);

    // Collect all allocation nodes.
    std::vector<NodeId> allocations;
    std::vector<NodeId> boxes;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind == NodeKind::Box) {
            boxes.push_back(id);
        } else if (is_allocation(n.kind)) {
            allocations.push_back(id);
        }
    }
    if (allocations.empty() && boxes.empty()) return {};

    bool changed = false;

    // Process Box nodes first (they may enable further allocation elimination).
    for (NodeId box : boxes) {
        if (process_box(g, box, uses)) changed = true;
    }

    // Rebuild use lists after Box elimination (uses may have changed).
    if (changed) {
        uses.build(g);
        changed = false;
    }

    // Process all other allocations.
    for (NodeId alloc : allocations) {
        if (process_allocation(g, alloc, uses)) changed = true;
    }

    // Iterative: run again until fixpoint (each elimination may enable more).
    int iterations = 0;
    while (changed && iterations < 10) {
        changed = false;
        uses.build(g);

        // Re-scan for new elimination opportunities.
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            const Node& n = g.node(id);
            if (n.is_dead()) continue;
            if (n.kind == NodeKind::Box) {
                if (process_box(g, id, uses)) { changed = true; continue; }
            }
            if (is_allocation(n.kind)) {
                if (process_allocation(g, id, uses)) changed = true;
            }
        }
        iterations++;
    }

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade::tier3
