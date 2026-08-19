// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/NCE.cpp

#include "jade/ir/passes/NCE.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

namespace jade {

namespace {

[[nodiscard]] bool is_provably_non_null(const Graph& g, NodeId id) {
    if (!id.valid() || id.value > g.size()) return false;
    const Node& n = g.node(id);
    switch (n.kind) {
        // Allocations are always non-null.
        case NodeKind::NewObj:
        case NodeKind::NewArr:
        case NodeKind::Allocate:
        case NodeKind::Box:
            return true;
        // A previous CheckNotNull already proved non-null.
        case NodeKind::CheckNotNull:
            return true;
        // LdStr is always non-null.
        case NodeKind::LdStr:
            return true;
        default:
            return false;
    }
}

}  // namespace

Result<void> NCEPass::run(Graph& g, PassContext& /*ctx*/) {
    bool changed = false;

    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind != NodeKind::CheckNotNull) continue;

        auto inputs = g.data_inputs(id);
        if (inputs.empty()) continue;
        const NodeId value = inputs[0];
        if (is_provably_non_null(g, value)) {
            g.mark_dead(id);
            changed = true;
        }
    }

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
