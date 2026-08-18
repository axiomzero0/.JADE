// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/ConstantFolding.cpp

#include "jade/ir/passes/ConstantFolding.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <cstdint>
#include <bit>

namespace jade {

namespace {

// .JADE integer arithmetic follows two's-complement wraparound semantics.
// granit uses int64_t throughout; overflow wraps (matching the C++ behavior
// for unsigned, but we explicitly wrap for signed using bit_cast).
[[nodiscard]] int64_t wrap_add(int64_t a, int64_t b) noexcept {
    return std::bit_cast<int64_t>(
        static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}
[[nodiscard]] int64_t wrap_sub(int64_t a, int64_t b) noexcept {
    return std::bit_cast<int64_t>(
        static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}
[[nodiscard]] int64_t wrap_mul(int64_t a, int64_t b) noexcept {
    return std::bit_cast<int64_t>(
        static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

// Try to fold a binary pure node whose inputs are both ConstInt (or
// previously-folded-to-ConstInt via the IsConst flag).
// Returns true if the node was rewritten to a constant.
[[nodiscard]] bool try_fold_binary(Graph& g, NodeId id) {
    Node& n = g.node(id);
    if (n.kind != NodeKind::Add && n.kind != NodeKind::Sub &&
        n.kind != NodeKind::Mul && n.kind != NodeKind::Div &&
        n.kind != NodeKind::Mod) {
        return false;
    }

    auto inputs = g.data_inputs(id);
    if (inputs.size() != 2) return false;

    const Node& a = g.node(inputs[0]);
    const Node& b = g.node(inputs[1]);
    // Accept either original ConstInt nodes or nodes we previously folded.
    const bool a_is_int = (a.kind == NodeKind::ConstInt) ||
                          (a.flags.has(NodeFlag::IsConst) && a.type == TypeId::Int);
    const bool b_is_int = (b.kind == NodeKind::ConstInt) ||
                          (b.flags.has(NodeFlag::IsConst) && b.type == TypeId::Int);
    if (!a_is_int || !b_is_int) return false;
    // Don't fold twice.
    if (n.flags.has(NodeFlag::IsConst)) return false;

    const int64_t av = g.side(inputs[0]).const_value.i64;
    const int64_t bv = g.side(inputs[1]).const_value.i64;
    int64_t result = 0;
    switch (n.kind) {
        case NodeKind::Add: result = wrap_add(av, bv); break;
        case NodeKind::Sub: result = wrap_sub(av, bv); break;
        case NodeKind::Mul: result = wrap_mul(av, bv); break;
        case NodeKind::Div:
            if (bv == 0) return false;  // deopt path; do not fold
            result = av / bv;           // .JADE integer division: truncation toward zero
            break;
        case NodeKind::Mod:
            if (bv == 0) return false;
            result = av % bv;
            break;
        default: return false;
    }

    // Rewrite the node into a const. We keep the original kind so downstream
    // passes (GVN, DCE) can still see what the original op was; we add the
    // IsConst flag and store the folded value. A future rewrite pass can
    // replace the kind with ConstInt once it's safe (e.g., after GVN has
    // de-duplicated).
    n.flags |= NodeFlag::IsConst;
    n.type   = TypeId::Int;
    g.side(id).const_value.i64 = result;
    return true;
}

}  // namespace

Result<void> ConstantFoldingPass::run(Graph& g, PassContext& /*ctx*/) {
    // Iterate to fixpoint so chained folds (e.g., Mul(Sub(20,5), 5) = 75)
    // are resolved in one pass invocation. Rule B.5 (idempotency) still
    // holds: a second invocation of the pass produces no further changes.
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            if (try_fold_binary(g, id)) {
                changed = true;
            }
        }
    }
    if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    return {};
}

}  // namespace jade
