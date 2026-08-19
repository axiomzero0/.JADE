// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/BCE.cpp

#include "jade/ir/passes/BCE.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

namespace jade {

namespace {

[[nodiscard]] bool get_const_int_value(const Graph& g, NodeId id, int64_t& out) {
    if (!id.valid() || id.value > g.size()) return false;
    const Node& n = g.node(id);
    if (n.kind != NodeKind::ConstInt) return false;
    out = g.side(id).const_value.i64;
    return true;
}

}  // namespace

Result<void> BCEPass::run(Graph& g, PassContext& /*ctx*/) {
    bool changed = false;

    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind != NodeKind::CheckBounds) continue;

        // CheckBounds has 2 inputs: index, length.
        auto inputs = g.data_inputs(id);
        if (inputs.size() != 2) continue;
        int64_t idx_val = 0, len_val = 0;
        if (!get_const_int_value(g, inputs[0], idx_val)) continue;
        if (!get_const_int_value(g, inputs[1], len_val)) continue;
        // If 0 <= idx < len, the check is provably true — eliminate it.
        if (idx_val >= 0 && idx_val < len_val) {
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
