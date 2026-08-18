// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/Graph.hpp
//
// The Sea of Nodes graph. Owns all nodes, edges, and side tables for one
// compilation. Thread-local — one Graph per compiler thread (Rule C.3).

#pragma once

#include "jade/core/NodeId.hpp"
#include "jade/core/Arena.hpp"
#include "jade/core/Result.hpp"
#include "jade/ir/Node.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"

#include <vector>
#include <span>
#include <string>

namespace jade {

// ─────────────────────────────────────────────────────────────────────────────
// Graph — owns the IR for one function compilation.
// ─────────────────────────────────────────────────────────────────────────────
class Graph {
public:
    Graph() = default;

    // No copy — Graph owns arena-allocated state.
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;
    Graph(Graph&&) noexcept = default;
    Graph& operator=(Graph&&) noexcept = default;

    // ── Node creation ─────────────────────────────────────────────────────
    //
    // Create a new node and return its NodeId. The caller is responsible
    // for wiring inputs (via set_data_inputs / set_ctrl / set_effect).
    [[nodiscard]] NodeId create(NodeKind kind);

    // Create a node with the given data inputs and (optionally) ctrl/effect.
    [[nodiscard]] NodeId create(NodeKind kind, std::span<const NodeId> data_inputs,
                                 NodeId ctrl = NodeId::invalid(),
                                 NodeId effect = NodeId::invalid());

    // Create a constant Int node.
    [[nodiscard]] NodeId create_const_int(int64_t value);
    [[nodiscard]] NodeId create_const_float(double value);
    [[nodiscard]] NodeId create_const_bool(bool value);
    [[nodiscard]] NodeId create_const_null();

    // ── Accessors ─────────────────────────────────────────────────────────
    [[nodiscard]] Node&       node(NodeId id)       { return nodes_[id.value - 1]; }
    [[nodiscard]] const Node& node(NodeId id) const  { return nodes_[id.value - 1]; }

    [[nodiscard]] NodeSideData&       side(NodeId id)       { return side_data_[id.value - 1]; }
    [[nodiscard]] const NodeSideData& side(NodeId id) const  { return side_data_[id.value - 1]; }

    [[nodiscard]] std::span<NodeId> data_inputs(NodeId id) {
        const EdgeSlice s = node(id).data_inputs;
        return edge_pool_.get_mut(s.first_edge, s.count);
    }
    [[nodiscard]] std::span<const NodeId> data_inputs(NodeId id) const {
        const EdgeSlice s = node(id).data_inputs;
        return edge_pool_.get(s.first_edge, s.count);
    }

    [[nodiscard]] NodeId ctrl_input(NodeId id) const {
        const EdgeSlice s = node(id).ctrl_input;
        return s.count == 0 ? NodeId::invalid() : edge_pool_.get(s.first_edge, 1)[0];
    }
    [[nodiscard]] NodeId effect_input(NodeId id) const {
        const EdgeSlice s = node(id).effect_input;
        return s.count == 0 ? NodeId::invalid() : edge_pool_.get(s.first_edge, 1)[0];
    }

    // ── Mutators ──────────────────────────────────────────────────────────
    void set_data_inputs(NodeId id, std::span<const NodeId> inputs);
    void set_ctrl_input(NodeId id, NodeId ctrl);
    void set_effect_input(NodeId id, NodeId effect);
    void set_frame_state(NodeId id, FrameStateId state);
    void mark_dead(NodeId id);

    // ── Whole-graph queries ────────────────────────────────────────────────
    [[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }
    [[nodiscard]] NodeId start_node() const noexcept {
        return start_id_.valid() ? start_id_ : NodeId::invalid();
    }
    [[nodiscard]] BumpAllocator& arena() noexcept { return arena_; }

    // Iterate all live node IDs.
    [[nodiscard]] std::span<const Node> all_nodes() const noexcept { return nodes_; }

    // ── Debug printing ────────────────────────────────────────────────────
    [[nodiscard]] std::string dump() const;
    [[nodiscard]] std::string dump_node(NodeId id) const;

private:
    std::vector<Node>         nodes_{};
    std::vector<NodeSideData> side_data_{};
    EdgePool                  edge_pool_{};
    BumpAllocator             arena_{};
    NodeId                    start_id_{};

    friend class GraphBuilder;

    [[nodiscard]] NodeId allocate_id() {
        const uint32_t id = static_cast<uint32_t>(nodes_.size()) + 1;  // 1-indexed
        nodes_.emplace_back();
        side_data_.emplace_back();
        return NodeId{id};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// GraphBuilder — convenience for building small graphs in tests and tier0.
// ─────────────────────────────────────────────────────────────────────────────
class GraphBuilder {
public:
    explicit GraphBuilder(Graph& g) : g_(g) {}

    [[nodiscard]] NodeId start();
    [[nodiscard]] NodeId const_int(int64_t v);
    [[nodiscard]] NodeId const_float(double v);
    [[nodiscard]] NodeId const_bool(bool v);
    [[nodiscard]] NodeId const_null();
    [[nodiscard]] NodeId add(NodeId a, NodeId b);
    [[nodiscard]] NodeId sub(NodeId a, NodeId b);
    [[nodiscard]] NodeId mul(NodeId a, NodeId b);
    [[nodiscard]] NodeId div(NodeId a, NodeId b);
    [[nodiscard]] NodeId cmp_lt(NodeId a, NodeId b);
    [[nodiscard]] NodeId if_node(NodeId cond);
    [[nodiscard]] NodeId return_node(NodeId value);
    [[nodiscard]] NodeId allocate(ShapeId shape);
    [[nodiscard]] NodeId load_field(NodeId obj, StringId field_id, uint16_t offset);
    [[nodiscard]] NodeId store_field(NodeId obj, StringId field_id, uint16_t offset, NodeId value);

private:
    Graph& g_;
};

}  // namespace jade
