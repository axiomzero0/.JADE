// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/SCCP.cpp
//
// Sparse Conditional Constant Propagation implementation.

#include "jade/ir/passes/SCCP.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <unordered_map>
#include <variant>
#include <vector>

namespace jade {

namespace {

// Lattice value: Top (unknown), a constant, or Bottom (overdefined).
struct LatticeValue {
    enum class Kind : uint8_t { Top, Const, Bottom };
    Kind kind = Kind::Top;
    int64_t const_value = 0;

    static LatticeValue top() { return {Kind::Top, 0}; }
    static LatticeValue bottom() { return {Kind::Bottom, 0}; }
    static LatticeValue constant(int64_t v) { return {Kind::Const, v}; }

    bool operator==(const LatticeValue& o) const noexcept {
        return kind == o.kind && (kind != Kind::Const || const_value == o.const_value);
    }
    bool is_top() const noexcept { return kind == Kind::Top; }
    bool is_const() const noexcept { return kind == Kind::Const; }
    bool is_bottom() const noexcept { return kind == Kind::Bottom; }
};

// Meet two lattice values.
LatticeValue meet(LatticeValue a, LatticeValue b) {
    if (a.is_top()) return b;
    if (b.is_top()) return a;
    if (a.is_bottom() || b.is_bottom()) return LatticeValue::bottom();
    if (a.const_value == b.const_value) return a;
    return LatticeValue::bottom();
}

// Abstract evaluation of a binary op.
LatticeValue eval_binary(NodeKind kind, int64_t a, int64_t b) {
    switch (kind) {
        case NodeKind::Add: return LatticeValue::constant(a + b);
        case NodeKind::Sub: return LatticeValue::constant(a - b);
        case NodeKind::Mul: return LatticeValue::constant(a * b);
        case NodeKind::Div:
            if (b == 0) return LatticeValue::top();   // deopt; don't propagate
            return LatticeValue::constant(a / b);
        case NodeKind::Mod:
            if (b == 0) return LatticeValue::top();
            return LatticeValue::constant(a % b);
        case NodeKind::And: return LatticeValue::constant(a & b);
        case NodeKind::Or:  return LatticeValue::constant(a | b);
        case NodeKind::Xor: return LatticeValue::constant(a ^ b);
        case NodeKind::Shl: return LatticeValue::constant(a << (b & 63));
        case NodeKind::Shr: return LatticeValue::constant(static_cast<uint64_t>(a) >> (b & 63));
        case NodeKind::Sar: return LatticeValue::constant(a >> (b & 63));
        case NodeKind::Eq:  return LatticeValue::constant(a == b ? 1 : 0);
        case NodeKind::Ne:  return LatticeValue::constant(a != b ? 1 : 0);
        case NodeKind::Lt:  return LatticeValue::constant(a < b ? 1 : 0);
        case NodeKind::Gt:  return LatticeValue::constant(a > b ? 1 : 0);
        case NodeKind::Lte: return LatticeValue::constant(a <= b ? 1 : 0);
        case NodeKind::Gte: return LatticeValue::constant(a >= b ? 1 : 0);
        default: return LatticeValue::top();
    }
}

}  // namespace

Result<void> SCCPPass::run(Graph& g, PassContext& /*ctx*/) {
    std::vector<LatticeValue> values(g.size(), LatticeValue::top());
    std::vector<bool> on_worklist(g.size(), true);
    std::vector<uint32_t> worklist;
    worklist.reserve(g.size());
    for (std::size_t i = 0; i < g.size(); ++i) {
        worklist.push_back(static_cast<uint32_t>(i));
    }

    auto get_val = [&](NodeId id) -> LatticeValue {
        if (!id.valid() || id.value > g.size()) return LatticeValue::top();
        return values[id.value - 1];
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            const Node& n = g.node(id);
            if (n.is_dead()) continue;

            LatticeValue old = values[i];
            LatticeValue neu = old;

            switch (n.kind) {
                case NodeKind::ConstInt:
                    neu = LatticeValue::constant(g.side(id).const_value.i64);
                    break;
                case NodeKind::Add: case NodeKind::Sub: case NodeKind::Mul:
                case NodeKind::Div: case NodeKind::Mod:
                case NodeKind::And: case NodeKind::Or: case NodeKind::Xor:
                case NodeKind::Shl: case NodeKind::Shr: case NodeKind::Sar:
                case NodeKind::Eq: case NodeKind::Ne:
                case NodeKind::Lt: case NodeKind::Gt:
                case NodeKind::Lte: case NodeKind::Gte: {
                    auto inputs = g.data_inputs(id);
                    if (inputs.size() != 2) { neu = LatticeValue::bottom(); break; }
                    LatticeValue a = get_val(inputs[0]);
                    LatticeValue b = get_val(inputs[1]);
                    if (a.is_top() || b.is_top()) { neu = LatticeValue::top(); break; }
                    if (a.is_bottom() || b.is_bottom()) { neu = LatticeValue::bottom(); break; }
                    neu = eval_binary(n.kind, a.const_value, b.const_value);
                    break;
                }
                default:
                    // Unknown node — treat as overdefined (safe).
                    neu = LatticeValue::bottom();
                    break;
            }

            if (!(neu == old)) {
                values[i] = neu;
                changed = true;
            }
        }
    }

    // Phase 2: rewrite constant-propagated nodes.
    // For each pure node whose value is now a known constant, mark it as
    // IsConst and store the value. A subsequent ConstantFolding pass will
    // pick this up and potentially fold downstream uses.
    bool changed_any = false;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (!n.is_pure()) continue;
        const LatticeValue& lv = values[i];
        if (lv.is_const() && !n.flags.has(NodeFlag::IsConst)) {
            n.flags |= NodeFlag::IsConst;
            n.type = TypeId::Int;
            g.side(id).const_value.i64 = lv.const_value;
            changed_any = true;
        }
    }

    if (changed_any) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
