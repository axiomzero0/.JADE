// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/DeadCodeElimination.cpp

#include "jade/ir/passes/DeadCodeElimination.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <vector>

namespace jade {

namespace {

// Count uses of `id` across the entire graph, ignoring uses from nodes
// already marked dead. O(N) per call; for the initial milestone this is
// acceptable. A faster pass uses an explicit use list.
[[nodiscard]] uint32_t count_uses(const Graph& g, NodeId id) {
    uint32_t uses = 0;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId other{static_cast<uint32_t>(i + 1)};
        if (other == id) continue;
        // Skip uses from nodes already marked dead — they will be swept.
        if (g.node(other).is_dead()) continue;
        for (NodeId in : g.data_inputs(other)) {
            if (in == id) ++uses;
        }
        if (g.ctrl_input(other) == id) ++uses;
        if (g.effect_input(other) == id) ++uses;
    }
    return uses;
}

}  // namespace

Result<void> DeadCodeEliminationPass::run(Graph& g, PassContext& /*ctx*/) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            Node& n = g.node(id);
            if (n.is_dead()) continue;
            if (n.is_effect()) continue;       // effectful — keep
            if (n.is_control()) continue;      // control-flow node — keep
            if (!n.is_pure()) continue;        // unknown state — keep

            if (count_uses(g, id) == 0) {
                g.mark_dead(id);
                changed = true;
            }
        }
    }
    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
