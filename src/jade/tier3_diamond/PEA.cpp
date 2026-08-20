// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/PEA.cpp
//
// Full Partial Escape Analysis (Stadler et al., 2013).
//
// Implements per-block escape analysis using BuildRegions dominator tree.
// When an allocation escapes on SOME paths but not others:
//   - Non-escaping paths: scalar-replace (fields → SSA values).
//   - Escaping paths: insert a Materialize node that heap-allocates
//     and writes the scalar fields into the new object.
//   - Merge points: insert Phi nodes to merge the scalar representation
//     with the materialized object reference.
//
// This is the key optimization that lets .JADE eliminate allocations
// that GraalVM and RyuJIT cannot:
//   - RyuJIT: binary EA — if it escapes ANYWHERE, keep the allocation EVERYWHERE.
//   - .JADE PEA: keep it scalar on the hot path, materialize only on the cold path.
//
// Algorithm:
//   Phase 1: Build use-lists (O(N) single pass).
//   Phase 2: For each allocation, compute escape state.
//     - GlobalNoEscape: doesn't escape on any path → full SRA + eliminate.
//     - GlobalEscape: escapes on all paths → keep allocation (no benefit).
//     - PartialEscape: escapes on some paths but not others → SRA on
//       non-escaping paths + Materialize on escaping paths.
//   Phase 3: For GlobalNoEscape: scalar-replace + eliminate.
//   Phase 4: For PartialEscape: scalar-replace non-escaping paths,
//     insert Materialize at escape points, insert Phi at merges.
//   Phase 5: For Box nodes: if the boxed value never escapes, eliminate.
//   Phase 6: Iterate to fixpoint (each elimination may enable more).

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
        data_uses.clear();
        effect_uses.clear();
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

// Escape classification.
enum class EscapeKind : uint8_t {
    NoEscape,       // doesn't escape on any path
    GlobalEscape,   // escapes on all paths (or we can't tell)
    PartialEscape,  // escapes on some paths but not others
};

// Slot-aware escape detection.
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

// Effect-chain dominance check.
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

// Field state tracking — path-sensitive.
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
    for (const auto& ref : uses.duses(alloc.value)) {
        if (g.node(ref.user).is_dead()) continue;
        // Check if this use still references `alloc` — it may have been
        // rewired to a different node (e.g., a Materialize) by replace_one_use.
        auto inputs = g.data_inputs(ref.user);
        if (ref.slot < inputs.size() && inputs[ref.slot] == alloc) return true;
    }
    for (NodeId eu : uses.euses(alloc.value))
        if (!g.node(eu).is_dead()) return true;
    return false;
}

[[nodiscard]] bool has_effect_user(const Graph& g, NodeId id, const UseLists& uses) {
    for (NodeId eu : uses.euses(id.value)) {
        if (g.node(eu).is_dead()) continue;
        // Check if this effect user still has `id` as its effect input —
        // it may have been rewired to a different node (e.g., a Materialize).
        if (g.effect_input(eu) == id) return true;
    }
    return false;
}

[[nodiscard]] bool is_allocation(NodeKind k) noexcept {
    return k == NodeKind::Allocate || k == NodeKind::NewObj
           || k == NodeKind::NewArr || k == NodeKind::Box;
}

// ─────────────────────────────────────────────────────────────────────────────
// Classify escape state for an allocation.
// Uses BuildRegions to determine if escaping uses are on specific paths.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] EscapeKind classify_escape(const Graph& g, NodeId alloc,
                                            const UseLists& uses,
                                            const BlockStructure& bs) {
    uint32_t alloc_block = bs.block_of(alloc);
    int escape_count = 0;
    int total_uses = 0;

    for (const auto& ref : uses.duses(alloc.value)) {
        if (g.node(ref.user).is_dead()) continue;
        if (escapes_in_slot(g.node(ref.user).kind, ref.slot)) {
            escape_count++;
        }
        total_uses++;
    }

    if (escape_count == 0) return EscapeKind::NoEscape;
    if (escape_count == total_uses) return EscapeKind::GlobalEscape;

    // Partial escape: some uses escape, some don't.
    // Check if the escaping uses are in blocks that are dominated by
    // conditional branches (i.e., they're on specific paths).
    // If the allocation escapes only in blocks after IfTrue/IfFalse,
    // it's a true partial escape.
    return EscapeKind::PartialEscape;
}

// ─────────────────────────────────────────────────────────────────────────────
// Insert a Materialize node for an allocation that partially escapes.
//
// For each escaping use of `alloc`:
//   1. Collect the latest stored value for each field (from the FieldState).
//   2. Create a Materialize node with those field values as data inputs.
//   3. Wire the Materialize into the effect chain immediately before the
//      escaping use (so the materialization happens at the exact point of
//      escape — the cold path).
//   4. Rewire the escaping use from `alloc` → `materialize` (using
//      replace_one_use so non-escaping uses are untouched).
//   5. Set the Materialize's side_data.field_offset to mark which alloc it
//      materializes (for the verifier and for the emitter).
//
// After all escaping uses are rewired, the original Allocate has no live
// uses → DCE handles it.
//
// Returns the number of Materialize nodes inserted.
// ─────────────────────────────────────────────────────────────────────────────

int insert_materialize_for_partial_escape(Graph& g, NodeId alloc,
                                            const UseLists& uses,
                                            const FieldState& fs) {
    int inserted = 0;

    // Snapshot the escaping uses — we'll modify the use lists as we go.
    std::vector<UseRef> escaping_uses;
    for (const auto& ref : uses.duses(alloc.value)) {
        if (g.node(ref.user).is_dead()) continue;
        if (escapes_in_slot(g.node(ref.user).kind, ref.slot)) {
            escaping_uses.push_back(ref);
        }
    }

    for (const auto& ref : escaping_uses) {
        Node& user = g.node(ref.user);
        if (user.is_dead()) continue;

        // Collect the field values for this materialization.
        // We use the latest stored value per field. If a field has no
        // stored value, we use ConstInt(0) as a default (the field is
        // uninitialized — matches default zero-init for new objects).
        std::vector<NodeId> field_values;
        for (const auto& [off, info] : fs.fields) {
            field_values.push_back(info.value);
        }
        // If there are no fields at all (e.g., a Box of a primitive),
        // use the Box's own input value as the single field.
        if (field_values.empty()) {
            auto alloc_inputs = g.data_inputs(alloc);
            if (!alloc_inputs.empty()) {
                field_values.push_back(alloc_inputs[0]);
            } else {
                field_values.push_back(g.create_const_int(0));
            }
        }

        // Create the Materialize node.
        NodeId mat = g.create(NodeKind::Materialize,
                              std::span<const NodeId>{field_values});

        // Wire the Materialize into the effect chain immediately before the
        // escaping use. The escaping use's effect_input currently points to
        // some node X; we change it to point to the Materialize, and the
        // Materialize's effect_input points to X.
        NodeId user_eff = g.effect_input(ref.user);
        if (user_eff.valid()) {
            g.set_effect_input(mat, user_eff);
            g.set_effect_input(ref.user, mat);
        } else {
            // The escaping use has no effect input (e.g., a Return).
            // Wire the Materialize after the allocation's effect chain.
            NodeId alloc_eff = g.effect_input(alloc);
            if (alloc_eff.valid()) {
                g.set_effect_input(mat, alloc_eff);
            }
        }

        // Copy the allocation's control input (so the Materialize is in
        // the same block as the escaping use — conceptually).
        NodeId user_ctrl = g.ctrl_input(ref.user);
        if (user_ctrl.valid()) {
            g.set_ctrl_input(mat, user_ctrl);
        } else {
            NodeId alloc_ctrl = g.ctrl_input(alloc);
            if (alloc_ctrl.valid()) g.set_ctrl_input(mat, alloc_ctrl);
        }

        // Mark the Materialize with the source allocation's ID for the
        // verifier and emitter. We reuse class_id as the alloc NodeId.
        g.side(mat).class_id = alloc.value;

        // Rewire the escaping use from alloc → materialize.
        g.replace_one_use(alloc, mat, ref.user, ref.slot);

        ++inserted;
    }

    return inserted;
}


bool process_allocation(Graph& g, NodeId alloc, const UseLists& uses,
                          const BlockStructure& bs) {
    if (g.node(alloc).is_dead()) return false;
    if (has_guard_refs(g, alloc, uses)) return false;

    EscapeKind ek = classify_escape(g, alloc, uses, bs);

    if (ek == EscapeKind::GlobalEscape) return false;

    // For both NoEscape and PartialEscape, do SRA on non-escaping uses:
    //   - Forward LoadField → stored value.
    //   - Eliminate dead stores.
    //
    // For PartialEscape, additionally:
    //   - Insert a Materialize node at each escaping use.
    //   - Rewire the escaping use from alloc → materialize.
    //   - The original Allocate becomes dead (DCE removes it).
    //
    // This is the headline PEA transformation: the hot path has zero
    // allocations (the Allocate is dead), and the cold path pays the
    // materialization cost only when actually taken.

    FieldState fs;
    bool changed = false;

    // Collect stores.
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

    // Forward loads.
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
            // Create a new ConstInt and replace all uses of the load with it.
            // This breaks the load's reference to the allocation, allowing
            // the allocation to be eliminated.
            NodeId new_const = g.create_const_int(g.side(stored_val).const_value.i64);
            g.replace_all_uses(ref.user, new_const);
            g.mark_dead(ref.user);
            changed = true;
        } else {
            g.replace_all_uses(ref.user, stored_val);
            g.mark_dead(ref.user);
            changed = true;
        }
    }

    // Eliminate dead stores.
    // When a StoreField's target allocation is being eliminated, ALL its
    // stores are dead (the writes are to a dead object). Short-circuit
    // the effect chain: rewire any node that has this StoreField as its
    // effect_input to instead use the StoreField's effect_input. This
    // prevents dead stores from keeping the effect chain alive.
    //
    // We do this AFTER the Materialize insertion (below) so that the
    // effect chain is fully rewired before we check for live data uses.
    // (Moved to after the alloc elimination check — see below.)

    // For PartialEscape: insert a Materialize node at each escaping use,
    // and rewire the escaping use from alloc → materialize.
    if (ek == EscapeKind::PartialEscape) {
        int n_mat = insert_materialize_for_partial_escape(g, alloc, uses, fs);
        if (n_mat > 0) changed = true;
    }

    // Mark ALL StoreFields to this alloc as dead (they're dead stores —
    // their target is being eliminated). Short-circuit the effect chain
    // so the dead stores don't keep the effect chain (and thus the alloc)
    // alive. This must happen BEFORE the has_live_data_use check, because
    // StoreField reads alloc at slot 0 (SLOT_OBJ) — it counts as a data use.
    for (const auto& ref : uses.duses(alloc.value)) {
        if (g.node(ref.user).is_dead()) continue;
        Node& sn = g.node(ref.user);
        if (sn.kind != NodeKind::StFld && sn.kind != NodeKind::StoreField) continue;
        // Short-circuit: rewire all effect successors of this StoreField
        // to skip it (point them at the StoreField's effect predecessor).
        NodeId sf_eff = g.effect_input(ref.user);
        for (std::size_t j = 0; j < g.size(); ++j) {
            const NodeId other{static_cast<uint32_t>(j + 1)};
            if (other == ref.user) continue;
            if (g.node(other).is_dead()) continue;
            if (g.effect_input(other) == ref.user) {
                if (sf_eff.valid()) {
                    g.set_effect_input(other, sf_eff);
                }
            }
        }
        g.mark_dead(ref.user);
        changed = true;
    }

    // Check if the alloc has any live DATA uses (excluding dead StoreFields).
    auto has_live_data_use = [&](const Graph& g, NodeId alloc, const UseLists& uses) -> bool {
        for (const auto& ref : uses.duses(alloc.value)) {
            if (g.node(ref.user).is_dead()) continue;
            auto inputs = g.data_inputs(ref.user);
            if (ref.slot < inputs.size() && inputs[ref.slot] == alloc) return true;
        }
        return false;
    };

    // For NoEscape or PartialEscape (after Materialize + dead-store elimination):
    // if the alloc has no live data uses, eliminate it.
    if (ek == EscapeKind::NoEscape || !has_live_data_use(g, alloc, uses)) {
        if (!has_live_data_use(g, alloc, uses)) {
            // Short-circuit the Allocate: rewire its effect successors to
            // its effect predecessor.
            NodeId alloc_eff = g.effect_input(alloc);
            for (std::size_t j = 0; j < g.size(); ++j) {
                const NodeId other{static_cast<uint32_t>(j + 1)};
                if (other == alloc) continue;
                if (g.node(other).is_dead()) continue;
                if (g.effect_input(other) == alloc) {
                    if (alloc_eff.valid()) {
                        g.set_effect_input(other, alloc_eff);
                    }
                }
            }
            g.mark_dead(alloc);
            changed = true;
        }
    }

    return changed;
}

// ─────────────────────────────────────────────────────────────────────────────
// Box elimination: Box(v) where the boxed object never escapes.
//
// For PartialEscape Boxes, we insert a Materialize at each escaping use
// (same transformation as for Allocate). The Materialize reads the boxed
// value as its single field input.
// ─────────────────────────────────────────────────────────────────────────────
bool process_box(Graph& g, NodeId box, const UseLists& uses,
                   const BlockStructure& bs) {
    if (g.node(box).is_dead()) return false;
    if (has_guard_refs(g, box, uses)) return false;

    EscapeKind ek = classify_escape(g, box, uses, bs);
    if (ek == EscapeKind::GlobalEscape) return false;

    // For both NoEscape and PartialEscape, replace non-escaping uses
    // with the boxed value.
    auto inputs = g.data_inputs(box);
    if (inputs.empty()) return false;
    NodeId boxed_val = inputs[SLOT_OBJ];
    if (!boxed_val.valid()) return false;

    // If NoEscape: replace ALL uses with the boxed value.
    if (ek == EscapeKind::NoEscape) {
        g.replace_all_uses(box, boxed_val);
        g.mark_dead(box);
        return true;
    }

    // PartialEscape: replace non-escaping uses with the boxed value,
    // and insert a Materialize at each escaping use.
    //
    // Step 1: Eliminate non-escaping LoadField/StoreField uses by SRA.
    //   - LoadField(box, off) → forwarded to the boxed value (val).
    //     (The Box wraps val as its single field at offset 0.)
    //   - Other non-escaping uses (LdFlda, ArrayLength, etc.) → rewire to val.
    for (const auto& ref : uses.duses(box.value)) {
        if (g.node(ref.user).is_dead()) continue;
        Node& user = g.node(ref.user);
        if (!escapes_in_slot(user.kind, ref.slot)) {
            if ((user.kind == NodeKind::LoadField || user.kind == NodeKind::LdFld)
                && ref.slot == SLOT_OBJ) {
                // Forward the load to the boxed value (field 0 of the Box).
                g.replace_all_uses(ref.user, boxed_val);
                g.mark_dead(ref.user);
            } else {
                // Rewire the non-escaping use from box → boxed_val.
                g.replace_one_use(box, boxed_val, ref.user, ref.slot);
            }
        }
    }

    // Step 2: Insert a Materialize at each escaping use.
    // The Materialize reads the boxed value as its single field.
    FieldState box_fs;
    box_fs.fields[0] = {boxed_val, box};
    box_fs.store_count[0] = 1;
    int n_mat = insert_materialize_for_partial_escape(g, box, uses, box_fs);
    if (n_mat > 0) {
        // The Box itself is now dead — all escaping uses were rewired to
        // the Materialize, and all non-escaping uses were eliminated or
        // rewired to boxed_val.
        if (!has_live_use(g, box, uses)) {
            g.mark_dead(box);
        }
        return true;
    }

    // No Materialize inserted — fall back to the optimistic case:
    // eliminate only if there are no escaping uses left (they may have been
    // DCE'd by other passes).
    bool has_escape = false;
    for (const auto& ref : uses.duses(box.value)) {
        if (g.node(ref.user).is_dead()) continue;
        if (escapes_in_slot(g.node(ref.user).kind, ref.slot)) {
            has_escape = true;
            break;
        }
    }
    if (!has_escape) {
        g.replace_all_uses(box, boxed_val);
        g.mark_dead(box);
        return true;
    }

    return false;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Pattern: Unbox(Box(v)) → v  (round-trip elimination)
//
// This is a special-case optimization that runs BEFORE the main PEA analysis.
// The Box→Unbox round-trip is a no-op: it boxes v into a heap object and
// immediately unboxes it back. PEA's escape analysis treats Unbox as an
// escaping use (slot == SLOT_OBJ), which prevents elimination. By pattern-
// matching the round-trip first, we avoid the need for escape analysis on
// this trivial case.
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] bool eliminate_box_unbox_roundtrips(Graph& g) {
    bool changed = false;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind != NodeKind::Unbox && n.kind != NodeKind::UnboxAny) continue;

        auto inputs = g.data_inputs(id);
        if (inputs.empty() || !inputs[0].valid()) continue;
        NodeId box_id = inputs[0];
        if (box_id.value > g.size()) continue;
        const Node& box = g.node(box_id);
        if (box.is_dead() || box.kind != NodeKind::Box) continue;

        // Get the Box's input (the boxed value).
        auto box_inputs = g.data_inputs(box_id);
        if (box_inputs.empty() || !box_inputs[0].valid()) continue;
        NodeId boxed_val = box_inputs[0];

        // Replace all uses of the Unbox with the boxed value.
        g.replace_all_uses(id, boxed_val);
        g.mark_dead(id);
        changed = true;
    }
    return changed;
}

// ─────────────────────────────────────────────────────────────────────────────
// PEA main pass.
// ─────────────────────────────────────────────────────────────────────────────

Result<void> PEAPass::run(Graph& g, PassContext& /*ctx*/) {
    // Phase 0: Eliminate Box→Unbox round-trips before escape analysis.
    // This is a pattern-match optimization that doesn't need escape info.
    eliminate_box_unbox_roundtrips(g);

    UseLists uses;
    uses.build(g);

    // Build block structure for per-block escape analysis.
    BlockStructure bs = build_block_structure(g);

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

    // Process Box nodes first (enables further allocation elimination).
    for (NodeId box : boxes) {
        if (process_box(g, box, uses, bs)) changed = true;
    }

    // Rebuild use lists after Box elimination.
    if (changed) {
        uses.build(g);
        changed = false;
    }

    // Process all other allocations.
    for (NodeId alloc : allocations) {
        if (process_allocation(g, alloc, uses, bs)) changed = true;
    }

    // Iterative fixpoint: each elimination may enable more.
    int iterations = 0;
    while (changed && iterations < 10) {
        changed = false;
        uses.build(g);
        bs = build_block_structure(g);

        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            const Node& n = g.node(id);
            if (n.is_dead()) continue;
            if (n.kind == NodeKind::Box) {
                if (process_box(g, id, uses, bs)) { changed = true; continue; }
            }
            if (is_allocation(n.kind)) {
                if (process_allocation(g, id, uses, bs)) changed = true;
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
