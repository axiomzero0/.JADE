// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_ir_graph.cpp

#include <gtest/gtest.h>
#include "jade/ir/Graph.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"

using namespace jade;

TEST(GraphTest, CreateReturnsValidNodeId) {
    Graph g;
    NodeId id = g.create(NodeKind::ConstInt);
    EXPECT_TRUE(id.valid());
    EXPECT_EQ(id.value, 1u);
}

TEST(GraphTest, CreateConstIntStoresValue) {
    Graph g;
    NodeId id = g.create_const_int(42);
    const Node& n = g.node(id);
    EXPECT_EQ(n.kind, NodeKind::ConstInt);
    EXPECT_TRUE(n.is_const());
    EXPECT_TRUE(n.is_pure());
    EXPECT_EQ(n.type, TypeId::Int);
    EXPECT_EQ(g.side(id).const_value.i64, 42);
}

TEST(GraphTest, CreateAddSetsCommutativeFlag) {
    Graph g;
    auto a = g.create_const_int(1);
    auto b = g.create_const_int(2);
    NodeId inputs[] = {a, b};
    auto add = g.create(NodeKind::Add, inputs);
    const Node& n = g.node(add);
    EXPECT_TRUE(n.flags.has(NodeFlag::Commutative));
    EXPECT_TRUE(n.flags.has(NodeFlag::Pure));
    EXPECT_FALSE(n.flags.has(NodeFlag::Effect));
}

TEST(GraphTest, SetDataInputsIsRetrievable) {
    Graph g;
    auto a = g.create_const_int(1);
    auto b = g.create_const_int(2);
    NodeId inputs[] = {a, b};
    auto add = g.create(NodeKind::Add, inputs);
    auto retrieved = g.data_inputs(add);
    EXPECT_EQ(retrieved.size(), 2u);
    EXPECT_EQ(retrieved[0], a);
    EXPECT_EQ(retrieved[1], b);
}

TEST(GraphTest, SetCtrlAndEffectInputs) {
    Graph g;
    auto start = g.create(NodeKind::Start);
    auto ret = g.create(NodeKind::Return);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    EXPECT_EQ(g.ctrl_input(ret), start);
    EXPECT_EQ(g.effect_input(ret), start);
}

TEST(GraphTest, MarkDeadSetsFlag) {
    Graph g;
    auto id = g.create_const_int(0);
    EXPECT_FALSE(g.node(id).is_dead());
    g.mark_dead(id);
    EXPECT_TRUE(g.node(id).is_dead());
}

TEST(GraphTest, DumpContainsNodeIdAndKind) {
    Graph g;
    GraphBuilder b(g);
    b.start();
    b.const_int(42);
    std::string s = g.dump();
    EXPECT_NE(s.find("Start"), std::string::npos);
    EXPECT_NE(s.find("ConstInt"), std::string::npos);
    EXPECT_NE(s.find("%1"), std::string::npos);
}

TEST(GraphTest, GraphBuilderAddNodesHaveCorrectKind) {
    Graph g;
    GraphBuilder b(g);
    auto start  = b.start();
    auto three  = b.const_int(3);
    auto four   = b.const_int(4);
    auto seven  = b.add(three, four);
    auto ret    = b.return_node(seven);
    EXPECT_EQ(g.node(three).kind, NodeKind::ConstInt);
    EXPECT_EQ(g.node(seven).kind,  NodeKind::Add);
    EXPECT_EQ(g.node(ret).kind,    NodeKind::Return);
    EXPECT_EQ(g.start_node(), start);
}

TEST(GraphTest, AllocateNodeIsEffect) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.allocate(ShapeId{1});
    EXPECT_TRUE(g.node(a).is_effect());
    EXPECT_FALSE(g.node(a).is_pure());
}

TEST(GraphTest, LoadFieldHasFieldIdAndOffset) {
    Graph g;
    GraphBuilder b(g);
    auto obj = b.allocate(ShapeId{1});
    auto lf = b.load_field(obj, StringId{1}, 16);
    EXPECT_EQ(g.side(lf).field_id, StringId{1});
    EXPECT_EQ(g.side(lf).field_offset, 16u);
}

TEST(GraphTest, MultipleGraphsAreIndependent) {
    Graph g1, g2;
    auto a1 = g1.create_const_int(1);
    auto a2 = g2.create_const_int(2);
    EXPECT_EQ(g1.side(a1).const_value.i64, 1);
    EXPECT_EQ(g2.side(a2).const_value.i64, 2);
}
