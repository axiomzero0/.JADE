// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/ControlFlowSimplification.cpp

#include "jade/ir/passes/ControlFlowSimplification.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

namespace jade {

Result<void> ControlFlowSimplificationPass::run(Graph& g, PassContext& /*ctx*/) {
    bool changed = false;

    // Look for If nodes whose condition is a known constant.
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind != NodeKind::If) continue;

        auto inputs = g.data_inputs(id);
        if (inputs.size() != 1) continue;
        const NodeId cond = inputs[0];
        if (!cond.valid() || cond.value > g.size()) continue;
        const Node& cond_node = g.node(cond);
        if (!cond_node.flags.has(NodeFlag::IsConst)) continue;

        // The condition is a known constant. We can't fully rewrite the
        // control flow without a block structure, but we can mark the If
        // node as dead (it will be a no-op in the emitter). The dead branch
        // is whichever side the constant doesn't select.
        //
        // For now, mark the If as dead — the downstream emitter will
        // treat it as a no-op (the constant-folded condition means the
        // branch is predictable; the lowerer should have already used the
        // right target).
        g.mark_dead(id);
        changed = true;
    }

    // Look for Return nodes with no data input (void return) — leave as is.

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
