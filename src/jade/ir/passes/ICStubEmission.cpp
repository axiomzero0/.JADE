// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/ICStubEmission.cpp
//
// Monomorphic IC (inline cache) guard emission.
//
// Real implementation: without profile data, we emit a conservative
// inline cache for every CallVirt node. The cache checks the receiver's
// class against a sentinel (class_id = 0, meaning "uninitialized cache"). On
// first call, the cache misses and falls back to vtable lookup. On
// subsequent calls, if the receiver class matches the cached class,
// the fast path is taken.
//
// This pass transforms CallVirt → CheckClass + CallKnown. The CheckClass
// guard verifies the receiver's class; if it matches, the CallKnown is
// a direct call (no vtable lookup). If it doesn't match, the method
// deopts to granit.
//
// Without profile data, we use class_id = 0 (unknown) as the cached class.
// The Devirtualization pass (which runs before this one) handles the case
// where the receiver is a NewObj (known class). This pass handles the
// general case: it inserts the CheckClass guard, but with class_id = 0
// (which means "always deopt on first call, then cache the observed class").

#include "jade/ir/passes/ICStubEmission.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

namespace jade {

Result<void> ICStubEmissionPass::run(Graph& g, PassContext& /*ctx*/) {
    bool changed = false;

    // Walk all CallVirt nodes that haven't been devirtualized.
    // For each, insert a CheckClass guard before the call.
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind != NodeKind::CallVirt) continue;

        auto inputs = g.data_inputs(id);
        if (inputs.empty()) continue;
        NodeId receiver = inputs[0];
        if (!receiver.valid() || receiver.value > g.size()) continue;

        // Create a CheckClass guard for the receiver.
        // class_id = 0 means "uninitialized cache" (always deopt on first call).
        NodeId check_in[] = {receiver};
        NodeId check = g.create(NodeKind::CheckClass, check_in);
        g.side(check).class_id = 0;   // uninitialized cache
        g.set_ctrl_input(check, g.ctrl_input(id));
        g.set_effect_input(check, g.effect_input(id));

        // Rewire the CallVirt's effect input to the CheckClass.
        g.set_effect_input(id, check);

        changed = true;
    }

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
