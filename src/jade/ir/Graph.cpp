// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/Graph.cpp

#include "jade/ir/Graph.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"

#include <format>
#include <string>

namespace jade {

// ─────────────────────────────────────────────────────────────────────────────
// Graph
// ─────────────────────────────────────────────────────────────────────────────
NodeId Graph::create(NodeKind kind) {
    NodeId id = allocate_id();
    Node& n = node(id);
    n.kind = kind;

    const NodeKindInfo& info = node_kind_info(kind);
    n.flags = NodeFlag::None;
    if (info.is_pure)    n.flags |= NodeFlag::Pure;
    if (info.is_effect)  n.flags |= NodeFlag::Effect;
    if (info.is_control) n.flags |= NodeFlag::Control;
    if (info.is_commutative) n.flags |= NodeFlag::Commutative;
    if (info.is_associative) n.flags |= NodeFlag::Associative;
    if (info.is_guard)   n.flags |= NodeFlag::IsGuard | NodeFlag::HasSideExit;
    n.arity_hint = info.num_data_inputs;
    return id;
}

NodeId Graph::create(NodeKind kind, std::span<const NodeId> data_inputs,
                      NodeId ctrl, NodeId effect) {
    NodeId id = create(kind);
    set_data_inputs(id, data_inputs);
    if (ctrl.valid())   set_ctrl_input(id, ctrl);
    if (effect.valid()) set_effect_input(id, effect);
    return id;
}

NodeId Graph::create_const_int(int64_t v) {
    NodeId id = create(NodeKind::ConstInt);
    Node& n = node(id);
    n.flags |= NodeFlag::IsConst;
    n.type  = TypeId::Int;
    side(id).const_value.i64 = v;
    return id;
}

NodeId Graph::create_const_float(double v) {
    NodeId id = create(NodeKind::ConstFloat);
    Node& n = node(id);
    n.flags |= NodeFlag::IsConst;
    n.type  = TypeId::Float;
    side(id).const_value.f64 = v;
    return id;
}

NodeId Graph::create_const_bool(bool v) {
    NodeId id = create(NodeKind::ConstBool);
    Node& n = node(id);
    n.flags |= NodeFlag::IsConst;
    n.type  = TypeId::Bool;
    side(id).const_value.b = v;
    return id;
}

NodeId Graph::create_const_null() {
    NodeId id = create(NodeKind::ConstNull);
    Node& n = node(id);
    n.flags |= NodeFlag::IsConst;
    n.type  = TypeId::Null;
    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mutators
// ─────────────────────────────────────────────────────────────────────────────
void Graph::set_data_inputs(NodeId id, std::span<const NodeId> inputs) {
    auto [first, span] = edge_pool_.alloc(static_cast<uint32_t>(inputs.size()));
    std::copy(inputs.begin(), inputs.end(), span.begin());
    node(id).data_inputs = EdgeSlice{first, static_cast<uint32_t>(inputs.size())};
}

void Graph::set_ctrl_input(NodeId id, NodeId ctrl) {
    auto [first, span] = edge_pool_.alloc(1);
    span[0] = ctrl;
    node(id).ctrl_input = EdgeSlice{first, 1};
}

void Graph::set_effect_input(NodeId id, NodeId effect) {
    auto [first, span] = edge_pool_.alloc(1);
    span[0] = effect;
    node(id).effect_input = EdgeSlice{first, 1};
}

void Graph::set_frame_state(NodeId id, FrameStateId state) {
    node(id).state = state;
    if (state.valid()) node(id).flags |= NodeFlag::HasState;
}

void Graph::mark_dead(NodeId id) {
    node(id).flags |= NodeFlag::IsDead;
}

void Graph::replace_all_uses(NodeId old_id, NodeId new_id) {
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        Node& n = nodes_[i];
        if (n.is_dead()) continue;
        // Replace data inputs.
        EdgeSlice s = n.data_inputs;
        if (s.count > 0) {
            auto inputs = edge_pool_.get_mut(s.first_edge, s.count);
            for (NodeId& in : inputs) {
                if (in == old_id) in = new_id;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug printing
// ─────────────────────────────────────────────────────────────────────────────
std::string Graph::dump_node(NodeId id) const {
    const Node& n = node(id);
    std::string out;
    out += std::format("%{} = {} ", id.value, node_kind_name(n.kind));
    out += std::format("[{}]", to_string(n.flags));
    if (n.type != TypeId::Top) {
        out += std::format(" type={}", type_id_name(n.type));
    }
    if (!n.data_inputs.empty()) {
        out += " ins=(";
        for (auto in : data_inputs(id)) {
            out += std::format("%{} ", in.value);
        }
        if (!out.empty() && out.back() == ' ') out.pop_back();
        out += ")";
    }
    if (auto c = ctrl_input(id); c.valid())   out += std::format(" ctrl=%{}", c.value);
    if (auto e = effect_input(id); e.valid()) out += std::format(" eff=%{}", e.value);
    if (n.has_state()) out += std::format(" state=#{}", n.state.value);
    return out;
}

std::string Graph::dump() const {
    std::string out;
    out.reserve(nodes_.size() * 64);
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        out += dump_node(NodeId{static_cast<uint32_t>(i + 1)});
        out += '\n';
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// GraphBuilder
// ─────────────────────────────────────────────────────────────────────────────
NodeId GraphBuilder::start() {
    NodeId id = g_.create(NodeKind::Start);
    g_.start_id_ = id;
    return id;
}

NodeId GraphBuilder::const_int(int64_t v)        { return g_.create_const_int(v); }
NodeId GraphBuilder::const_float(double v)        { return g_.create_const_float(v); }
NodeId GraphBuilder::const_bool(bool v)           { return g_.create_const_bool(v); }
NodeId GraphBuilder::const_null()                  { return g_.create_const_null(); }

NodeId GraphBuilder::add(NodeId a, NodeId b) {
    NodeId inputs[] = {a, b};
    return g_.create(NodeKind::Add, inputs);
}

NodeId GraphBuilder::sub(NodeId a, NodeId b) {
    NodeId inputs[] = {a, b};
    return g_.create(NodeKind::Sub, inputs);
}

NodeId GraphBuilder::mul(NodeId a, NodeId b) {
    NodeId inputs[] = {a, b};
    return g_.create(NodeKind::Mul, inputs);
}

NodeId GraphBuilder::div(NodeId a, NodeId b) {
    NodeId inputs[] = {a, b};
    return g_.create(NodeKind::Div, inputs);
}

NodeId GraphBuilder::cmp_lt(NodeId a, NodeId b) {
    NodeId inputs[] = {a, b};
    return g_.create(NodeKind::Lt, inputs);
}

NodeId GraphBuilder::if_node(NodeId cond) {
    NodeId inputs[] = {cond};
    return g_.create(NodeKind::If, inputs);
}

NodeId GraphBuilder::return_node(NodeId value) {
    NodeId inputs[] = {value};
    return g_.create(NodeKind::Return, inputs);
}

NodeId GraphBuilder::allocate(ShapeId shape) {
    NodeId id = g_.create(NodeKind::Allocate);
    g_.side(id).shape_id = shape;
    return id;
}

NodeId GraphBuilder::load_field(NodeId obj, StringId field_id, uint16_t offset) {
    NodeId inputs[] = {obj};
    NodeId id = g_.create(NodeKind::LoadField, inputs);
    g_.side(id).field_id = field_id;
    g_.side(id).field_offset = offset;
    return id;
}

NodeId GraphBuilder::store_field(NodeId obj, StringId field_id, uint16_t offset, NodeId value) {
    NodeId inputs[] = {obj, value};
    NodeId id = g_.create(NodeKind::StoreField, inputs);
    g_.side(id).field_id = field_id;
    g_.side(id).field_offset = offset;
    return id;
}

}  // namespace jade
