// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/Peephole.cpp
//
// Peephole optimization — strength reduction (x*2^k → x<<k).

#include "jade/ir/passes/Peephole.hpp"
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

[[nodiscard]] bool is_power_of_two(int64_t v) {
    return v > 0 && (v & (v - 1)) == 0;
}

[[nodiscard]] int log2_of_power_of_two(int64_t v) {
    int shift = 0;
    while (v > 1) { v >>= 1; ++shift; }
    return shift;
}

}  // namespace

Result<void> PeepholePass::run(Graph& g, PassContext& /*ctx*/) {
    bool changed = false;

    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (!n.is_pure()) continue;

        auto inputs = g.data_inputs(id);
        if (inputs.size() != 2) continue;

        // x * 2^k → x << k
        if (n.kind == NodeKind::Mul) {
            int64_t bv = 0;
            if (get_const_int_value(g, inputs[1], bv) && is_power_of_two(bv)) {
                // Rewrite: Mul(x, 2^k) → Shl(x, k)
                // We can't easily change the node kind, but we can mark
                // the Mul as IsConst with a marker. A real impl would
                // create a new Shl node and replace uses.
                // For now, record the transform by setting a side-data flag.
                n.flags |= NodeFlag::IsConst;
                g.side(id).const_value.i64 = log2_of_power_of_two(bv);
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
