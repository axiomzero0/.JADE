// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/AlgebraicSimplification.cpp

#include "jade/ir/passes/AlgebraicSimplification.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <cstdint>

namespace jade {

namespace {

// Returns true if `id` is a ConstInt with value `v`.
[[nodiscard]] bool is_const_int(const Graph& g, NodeId id, int64_t v) {
    if (!id.valid() || id.value > g.size()) return false;
    const Node& n = g.node(id);
    if (n.kind != NodeKind::ConstInt) return false;
    return g.side(id).const_value.i64 == v;
}

// Returns true if `id` is a ConstInt with any value (writes to *out).
[[nodiscard]] bool get_const_int(const Graph& g, NodeId id, int64_t& out) {
    if (!id.valid() || id.value > g.size()) return false;
    const Node& n = g.node(id);
    if (n.kind != NodeKind::ConstInt) return false;
    out = g.side(id).const_value.i64;
    return true;
}

}  // namespace

Result<void> AlgebraicSimplificationPass::run(Graph& g, PassContext& /*ctx*/) {
    bool changed = false;

    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (!n.is_pure()) continue;

        auto inputs = g.data_inputs(id);
        if (inputs.size() != 2) continue;

        NodeId a = inputs[0];
        NodeId b = inputs[1];
        int64_t bv = 0;

        switch (n.kind) {
            // x + 0 → x  and  0 + x → x  (commutative)
            case NodeKind::Add:
                if (is_const_int(g, b, 0)) {
                    n.flags |= NodeFlag::IsConst;
                    g.side(id).const_value.i64 = 0;  // marker; will be replaced by DCE
                    changed = true;
                } else if (is_const_int(g, a, 0)) {
                    n.flags |= NodeFlag::IsConst;
                    g.side(id).const_value.i64 = 0;
                    changed = true;
                }
                break;
            // x - 0 → x
            case NodeKind::Sub:
                if (is_const_int(g, b, 0)) {
                    n.flags |= NodeFlag::IsConst;
                    g.side(id).const_value.i64 = 0;
                    changed = true;
                }
                break;
            // x * 1 → x
            case NodeKind::Mul:
                if (is_const_int(g, b, 1) || is_const_int(g, a, 1)) {
                    n.flags |= NodeFlag::IsConst;
                    g.side(id).const_value.i64 = 1;  // marker
                    changed = true;
                }
                // x * 0 → 0
                else if (is_const_int(g, b, 0) || is_const_int(g, a, 0)) {
                    n.flags |= NodeFlag::IsConst;
                    n.type = TypeId::Int;
                    g.side(id).const_value.i64 = 0;
                    changed = true;
                }
                break;
            // x / 1 → x
            case NodeKind::Div:
                if (is_const_int(g, b, 1)) {
                    n.flags |= NodeFlag::IsConst;
                    g.side(id).const_value.i64 = 1;  // marker; result is `a`
                    changed = true;
                }
                break;
            // x & 0 → 0
            case NodeKind::And:
                if (is_const_int(g, b, 0) || is_const_int(g, a, 0)) {
                    n.flags |= NodeFlag::IsConst;
                    n.type = TypeId::Int;
                    g.side(id).const_value.i64 = 0;
                    changed = true;
                }
                // x & x → x (only if same NodeId)
                else if (a == b) {
                    n.flags |= NodeFlag::IsConst;
                    g.side(id).const_value.i64 = 0;  // marker
                    changed = true;
                }
                break;
            // x | 0 → x
            case NodeKind::Or:
                if (is_const_int(g, b, 0) || is_const_int(g, a, 0)) {
                    n.flags |= NodeFlag::IsConst;
                    g.side(id).const_value.i64 = 0;  // marker
                    changed = true;
                }
                // x | x → x
                else if (a == b) {
                    n.flags |= NodeFlag::IsConst;
                    g.side(id).const_value.i64 = 0;
                    changed = true;
                }
                break;
            // x ^ 0 → x
            case NodeKind::Xor:
                if (is_const_int(g, b, 0) || is_const_int(g, a, 0)) {
                    n.flags |= NodeFlag::IsConst;
                    g.side(id).const_value.i64 = 0;  // marker
                    changed = true;
                }
                // x ^ x → 0
                else if (a == b) {
                    n.flags |= NodeFlag::IsConst;
                    n.type = TypeId::Int;
                    g.side(id).const_value.i64 = 0;
                    changed = true;
                }
                break;
            // x << 0 → x   x >> 0 → x
            case NodeKind::Shl:
            case NodeKind::Shr:
            case NodeKind::Sar:
                if (is_const_int(g, b, 0)) {
                    n.flags |= NodeFlag::IsConst;
                    g.side(id).const_value.i64 = 0;  // marker
                    changed = true;
                }
                break;
            default:
                break;
        }
        (void)get_const_int;
    }

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
