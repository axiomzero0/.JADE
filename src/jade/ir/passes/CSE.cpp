// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/CSE.cpp

#include "jade/ir/passes/CSE.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <unordered_map>
#include <vector>
#include <span>
#include <cstdint>
#include <algorithm>

namespace jade {

namespace {

// Signature for a pure node — kind + sorted inputs (commutative) + const value.
struct Sig {
    NodeKind kind;
    uint32_t arity;
    std::vector<NodeId> inputs;
    int64_t const_val = 0;
    bool has_const = false;

    bool operator==(const Sig& o) const noexcept {
        return kind == o.kind && arity == o.arity && inputs == o.inputs
               && has_const == o.has_const && (!has_const || const_val == o.const_val);
    }
};

struct SigHash {
    std::size_t operator()(const Sig& s) const noexcept {
        std::uint64_t h = 14695981039346656037ULL;
        auto mix = [&](std::uint64_t v) { h ^= v; h *= 1099511628211ULL; };
        mix(static_cast<std::uint64_t>(s.kind));
        mix(s.arity);
        for (NodeId in : s.inputs) mix(in.value);
        if (s.has_const) mix(static_cast<std::uint64_t>(s.const_val));
        return static_cast<std::size_t>(h);
    }
};

[[nodiscard]] Sig make_signature(const Graph& g, NodeId id) {
    const Node& n = g.node(id);
    Sig s;
    s.kind = n.kind;
    auto inputs = g.data_inputs(id);
    s.arity = static_cast<uint32_t>(inputs.size());
    s.inputs.assign(inputs.begin(), inputs.end());
    if (n.flags.has(NodeFlag::IsConst) && n.kind == NodeKind::ConstInt) {
        s.has_const = true;
        s.const_val = g.side(id).const_value.i64;
    }
    if (n.flags.has(NodeFlag::Commutative)) {
        std::sort(s.inputs.begin(), s.inputs.end());
    }
    return s;
}

}  // namespace

Result<void> CSEPass::run(Graph& g, PassContext& /*ctx*/) {
    // Since we don't have basic block structure yet, we treat the entire
    // graph as one block. The "no intervening effectful node" rule is
    // approximated by: if a later node has the same signature as an earlier
    // pure node AND no effectful node was emitted between them, dedup.
    //
    // In practice, since we walk in NodeId order and the lowerer emits
    // effectful nodes inline, this is conservative but correct.
    std::unordered_map<Sig, NodeId, SigHash> table;
    bool changed = false;

    bool seen_effect_since_last_dedup = false;

    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        Node& n = g.node(id);
        if (n.is_dead()) continue;

        if (n.is_effect()) {
            // Effectful node — invalidate the table (a store could change
            // what a later load returns).
            table.clear();
            seen_effect_since_last_dedup = false;
            continue;
        }

        if (!n.is_pure()) continue;

        Sig sig = make_signature(g, id);
        auto [it, inserted] = table.try_emplace(sig, id);
        if (inserted) continue;

        // Duplicate found: rewire all uses of this node to point to the
        // surviving node, then mark dead.
        g.replace_all_uses(id, it->second);
        g.mark_dead(id);
        changed = true;
        (void)seen_effect_since_last_dedup;
    }

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
