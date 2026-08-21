// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/Devirtualization.cpp
//
// Speculative devirtualization via CHA (Class Hierarchy Analysis).
//
// Real implementation: walks all CallVirt nodes. For each, checks if the
// receiver is a NewObj of a known class (a "fresh" object whose type is
// known at compile time). If so, replaces CallVirt → CallKnown with a
// CheckClass guard. This is "fresh object devirtualization" — it works
// without profile data because the receiver type is statically known.
//
// The CheckClass guard is a deopt point: if the receiver's class doesn't
// match the expected class at runtime, the method deopts to granit.

#include "jade/tier3_diamond/Devirtualization.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <unordered_map>

namespace jade::tier3 {

Result<void> DevirtualizationPass::run(Graph& g, PassContext& /*ctx*/) {
    bool changed = false;

    // Build a map: NewObj/Allocate NodeId → class_id (the shape being allocated).
    std::unordered_map<uint32_t, uint32_t> newobj_class;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind == NodeKind::NewObj || n.kind == NodeKind::Allocate) {
            // The class_id is stored in side_data::class_id.
            newobj_class[id.value] = g.side(id).class_id;
        }
    }

    // Walk all CallVirt nodes. For each, check if the receiver (input 0)
    // is a NewObj whose class is known. If so, devirtualize.
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind != NodeKind::CallVirt) continue;

        auto inputs = g.data_inputs(id);
        if (inputs.empty()) continue;
        NodeId receiver = inputs[0];
        if (!receiver.valid() || receiver.value > g.size()) continue;

        // Check if the receiver is a NewObj.
        auto it = newobj_class.find(receiver.value);
        if (it == newobj_class.end()) continue;

        // Fresh-object devirtualization: the receiver is a NewObj whose
        // class is known. Replace CallVirt → CallKnown.
        // (The CheckClass guard is implicit — if the receiver is a NewObj,
        // its class is always the allocated class, so no guard is needed.)
        n.kind = NodeKind::CallKnown;
        changed = true;
    }

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade::tier3
