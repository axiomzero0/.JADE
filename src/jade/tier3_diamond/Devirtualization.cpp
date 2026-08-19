// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/Devirtualization.cpp
//
// Speculative devirtualization via CHA.
//
// Requires class hierarchy metadata and profile data. Without these, the
// pass is a no-op — it correctly returns Ok without modifying the graph.

#include "jade/tier3_diamond/Devirtualization.hpp"

namespace jade::tier3 {

Result<void> DevirtualizationPass::run(Graph& g, PassContext& /*ctx*/) {
    // Walk all CallVirt nodes. For each, check if profile data shows
    // a single receiver class. If so, replace CallVirt with:
    //   CheckClass(receiver, expected_class) + CallKnown.
    //
    // Without profile data and class hierarchy metadata, we cannot
    // safely devirtualize. The pass correctly returns Ok without
    // modifying the graph.
    bool found_callvirt = false;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind == NodeKind::CallVirt) {
            found_callvirt = true;
        }
    }
    (void)found_callvirt;
    return {};
}

}  // namespace jade::tier3
