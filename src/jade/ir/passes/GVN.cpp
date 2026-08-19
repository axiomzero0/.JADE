// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/GVN.cpp

#include "jade/ir/passes/GVN.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <span>
#include <cstdint>

namespace jade {

namespace {

// A canonical signature for a pure node, used as the GVN hash key.
struct NodeSignature {
    NodeKind kind;
    uint32_t arity;
    // Hashed signature; we normalize commutative inputs by sorting.
    std::vector<NodeId> inputs;
    // For constants — include the constant value.
    int64_t const_int_value{0};

    bool operator==(const NodeSignature&) const = default;
};

struct SigHash {
    std::size_t operator()(const NodeSignature& s) const noexcept {
        // FNV-1a 64-bit.
        std::uint64_t h = 14695981039346656037ULL;
        auto mix = [&](std::uint64_t v) {
            h ^= v;
            h *= 1099511628211ULL;
        };
        mix(static_cast<std::uint64_t>(s.kind));
        mix(s.arity);
        for (NodeId in : s.inputs) mix(in.value);
        mix(static_cast<std::uint64_t>(s.const_int_value));
        return static_cast<std::size_t>(h);
    }
};

[[nodiscard]] NodeSignature make_signature(const Graph& g, NodeId id) {
    const Node& n = g.node(id);
    NodeSignature sig;
    sig.kind  = n.kind;
    sig.arity = static_cast<uint32_t>(g.data_inputs(id).size());

    if (n.is_const() && n.kind == NodeKind::ConstInt) {
        sig.const_int_value = g.side(id).const_value.i64;
        return sig;
    }

    auto inputs = g.data_inputs(id);
    sig.inputs.assign(inputs.begin(), inputs.end());

    // Commutative normalization (Rule 1.6):
    if (n.flags.has(NodeFlag::Commutative)) {
        std::sort(sig.inputs.begin(), sig.inputs.end());
    }
    return sig;
}

}  // namespace

Result<void> GVNPass::run(Graph& g, PassContext& /*ctx*/) {
    std::unordered_map<NodeSignature, NodeId, SigHash> table;

    bool changed = false;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (!n.is_pure()) continue;  // GVN only on pure nodes for now

        NodeSignature sig = make_signature(g, id);
        auto [it, inserted] = table.try_emplace(sig, id);
        if (inserted) continue;

        // Duplicate found: replace this node with the existing one.
        // Rewire all uses of this node to point to the surviving node,
        // then mark this one dead. DCE will sweep it.
        g.replace_all_uses(id, it->second);
        g.mark_dead(id);
        changed = true;
    }

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
