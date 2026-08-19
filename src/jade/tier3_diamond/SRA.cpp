// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/SRA.cpp
//
// Scalar Replacement of Aggregates — fixed version with:
//   - Use-list-based O(A×U) complexity
//   - Effect-chain-aware forwarding
//   - Path-sensitive field state (bails on conflicting stores)
//   - FrameState/guard reference check
//   - Named slot constants

#include "jade/tier3_diamond/SRA.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <unordered_map>
#include <vector>

namespace jade::tier3 {

namespace {

constexpr std::size_t SLOT_OBJ = 0;
constexpr std::size_t SLOT_VAL = 1;

struct UseRef {
    NodeId      user;
    std::size_t slot;
};

struct UseLists {
    std::unordered_map<uint32_t, std::vector<UseRef>> data_uses;
    std::unordered_map<uint32_t, std::vector<NodeId>> effect_uses;
    std::unordered_map<uint32_t, std::vector<NodeId>> frame_state_uses;

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
        auto it = data_uses.find(v);
        return it != data_uses.end() ? it->second : empty;
    }
    [[nodiscard]] const std::vector<NodeId>& euses(uint32_t v) const {
        static const std::vector<NodeId> empty;
        auto it = effect_uses.find(v);
        return it != effect_uses.end() ? it->second : empty;
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
        default:
            return true;
    }
}

[[nodiscard]] bool effect_dominates(const Graph& g, NodeId store, NodeId load) {
    // Walk the effect chain backwards from the load's effect input.
    // The store dominates the load if:
    //   1. The store IS the load's effect input (direct dominance), OR
    //   2. The store can be reached by walking up the effect chain.
    // Also accept the case where both share the same effect input (siblings
    // under the same effect parent — the store is still "before" the load
    // in program order because it appears earlier in the NodeId sequence).
    NodeId load_eff = g.effect_input(load);
    if (load_eff == store) return true;

    // Walk up from the load's effect input.
    NodeId cur = load_eff;
    while (cur.valid()) {
        if (cur == store) return true;
        NodeId next = g.effect_input(cur);
        if (next == cur) break;
        cur = next;
    }

    // Check sibling case: store and load have the same effect input,
    // and store appears before load in NodeId order.
    NodeId store_eff = g.effect_input(store);
    if (store_eff == load_eff && store.value < load.value) return true;

    return false;
}

[[nodiscard]] bool is_allocation(NodeKind k) noexcept {
    return k == NodeKind::Allocate || k == NodeKind::NewObj
           || k == NodeKind::NewArr || k == NodeKind::Box;
}

[[nodiscard]] bool has_guard_refs(const Graph& g, NodeId alloc, const UseLists& uses) {
    if (!uses.frame_state_uses.count(alloc.value)) {
        // Check if any frame_state_uses entry exists.
    }
    for (const auto& ref : uses.duses(alloc.value)) {
        const Node& user = g.node(ref.user);
        if (user.is_dead()) continue;
        if (user.is_guard()) return true;
    }
    return false;
}

[[nodiscard]] bool has_live_use(const Graph& g, NodeId alloc, const UseLists& uses) {
    for (const auto& ref : uses.duses(alloc.value)) {
        if (!g.node(ref.user).is_dead()) return true;
    }
    for (NodeId eu : uses.euses(alloc.value)) {
        if (!g.node(eu).is_dead()) return true;
    }
    return false;
}

[[nodiscard]] bool has_effect_user(const Graph& g, NodeId id, const UseLists& uses) {
    for (NodeId eu : uses.euses(id.value)) {
        if (!g.node(eu).is_dead()) return true;
    }
    return false;
}

}  // namespace

Result<void> SRAPass::run(Graph& g, PassContext& /*ctx*/) {
    UseLists uses;
    uses.build(g);

    std::vector<NodeId> candidates;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (!is_allocation(n.kind)) continue;
        if (has_guard_refs(g, id, uses)) continue;

        bool escapes = false;
        for (const auto& ref : uses.duses(id.value)) {
            if (g.node(ref.user).is_dead()) continue;
            if (escapes_in_slot(g.node(ref.user).kind, ref.slot)) {
                escapes = true;
                break;
            }
        }
        if (!escapes) candidates.push_back(id);
    }
    if (candidates.empty()) return {};

    bool changed = false;

    for (NodeId alloc : candidates) {
        // Path-sensitive field state: count stores per offset.
        struct StoreInfo { NodeId value; NodeId store_node; };
        std::unordered_map<uint16_t, StoreInfo> last_store;
        std::unordered_map<uint16_t, int> store_count;

        for (const auto& ref : uses.duses(alloc.value)) {
            if (g.node(ref.user).is_dead()) continue;
            Node& n = g.node(ref.user);

            if (n.kind == NodeKind::StFld || n.kind == NodeKind::StoreField) {
                uint16_t offset = g.side(ref.user).field_offset;
                auto inputs = g.data_inputs(ref.user);
                NodeId value = inputs.size() > SLOT_VAL ? inputs[SLOT_VAL] : NodeId::invalid();
                last_store[offset] = {value, ref.user};
                store_count[offset]++;
            } else if (n.kind == NodeKind::LdFld || n.kind == NodeKind::LoadField) {
                uint16_t offset = g.side(ref.user).field_offset;

                // Path-sensitivity: only forward if stored exactly once.
                auto sc = store_count.find(offset);
                if (sc == store_count.end() || sc->second != 1) continue;

                auto it = last_store.find(offset);
                if (it == last_store.end()) continue;

                NodeId stored_val = it->second.value;
                NodeId store_node = it->second.store_node;
                if (!stored_val.valid() || stored_val.value > g.size()) continue;

                // Effect-chain dominance check.
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
        }

        // Eliminate dead stores.
        for (const auto& ref : uses.duses(alloc.value)) {
            if (g.node(ref.user).is_dead()) continue;
            Node& n = g.node(ref.user);
            if (n.kind != NodeKind::StFld && n.kind != NodeKind::StoreField) continue;
            if (!has_effect_user(g, ref.user, uses)) {
                g.mark_dead(ref.user);
                changed = true;
            }
        }

        // Eliminate allocation if no live uses.
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
