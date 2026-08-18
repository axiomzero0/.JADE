// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_ir_csharp_nodes.cpp
//
// Tests for the C#-specific NodeKind extensions (Box, Unbox, IsInst, etc.).

#include <gtest/gtest.h>
#include "jade/ir/Graph.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

using namespace jade;

TEST(CSharpNodesTest, BoxNodeIsEffectfulAndNotPure) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto i = b.const_int(42);
    NodeId inputs[] = {i};
    auto box = g.create(NodeKind::Box, inputs);
    g.set_effect_input(box, start);
    const Node& n = g.node(box);
    EXPECT_TRUE(n.is_effect());
    EXPECT_FALSE(n.is_pure());
}

TEST(CSharpNodesTest, UnboxNodeIsEffectful) {
    Graph g;
    auto obj = g.create(NodeKind::LdNull);
    NodeId inputs[] = {obj};
    auto unbox = g.create(NodeKind::Unbox, inputs);
    const Node& n = g.node(unbox);
    EXPECT_TRUE(n.is_effect());
    EXPECT_FALSE(n.is_pure());
}

TEST(CSharpNodesTest, IsInstNodeIsEffectful) {
    Graph g;
    auto obj = g.create(NodeKind::LdNull);
    NodeId inputs[] = {obj};
    auto isinst = g.create(NodeKind::IsInst, inputs);
    EXPECT_TRUE(g.node(isinst).is_effect());
}

TEST(CSharpNodesTest, CastClassNodeIsEffectfulAndCanThrow) {
    Graph g;
    auto obj = g.create(NodeKind::LdNull);
    NodeId inputs[] = {obj};
    auto cast = g.create(NodeKind::CastClass, inputs);
    EXPECT_TRUE(g.node(cast).is_effect());
}

TEST(CSharpNodesTest, LdNullIsPure) {
    Graph g;
    auto n = g.create(NodeKind::LdNull);
    EXPECT_TRUE(g.node(n).is_pure());
    EXPECT_FALSE(g.node(n).is_effect());
    EXPECT_EQ(g.node(n).kind, NodeKind::LdNull);
}

TEST(CSharpNodesTest, LdStrIsPure) {
    Graph g;
    auto n = g.create(NodeKind::LdStr);
    EXPECT_TRUE(g.node(n).is_pure());
    EXPECT_FALSE(g.node(n).is_effect());
}

TEST(CSharpNodesTest, NewObjIsEffectful) {
    Graph g;
    auto n = g.create(NodeKind::NewObj);
    EXPECT_TRUE(g.node(n).is_effect());
    EXPECT_FALSE(g.node(n).is_pure());
}

TEST(CSharpNodesTest, CallVirtIsEffectful) {
    Graph g;
    auto n = g.create(NodeKind::CallVirt);
    EXPECT_TRUE(g.node(n).is_effect());
}

TEST(CSharpNodesTest, ThrowIsControlAndEffect) {
    Graph g;
    auto n = g.create(NodeKind::Throw);
    EXPECT_TRUE(g.node(n).is_effect());
    EXPECT_TRUE(g.node(n).is_control());
}

TEST(CSharpNodesTest, LdFldIsEffectful) {
    Graph g;
    auto obj = g.create(NodeKind::LdNull);
    NodeId inputs[] = {obj};
    auto ldfld = g.create(NodeKind::LdFld, inputs);
    EXPECT_TRUE(g.node(ldfld).is_effect());
}

TEST(CSharpNodesTest, StFldIsEffectfulWithTwoInputs) {
    Graph g;
    auto obj = g.create(NodeKind::LdNull);
    auto val = g.create_const_int(0);
    NodeId inputs[] = {obj, val};
    auto stfld = g.create(NodeKind::StFld, inputs);
    EXPECT_TRUE(g.node(stfld).is_effect());
    EXPECT_EQ(g.data_inputs(stfld).size(), 2u);
}

TEST(CSharpNodesTest, LdElemIsEffectfulWithArrayAndIndex) {
    Graph g;
    auto arr = g.create(NodeKind::LdNull);
    auto idx = g.create_const_int(0);
    NodeId inputs[] = {arr, idx};
    auto ldelem = g.create(NodeKind::LdElem, inputs);
    EXPECT_TRUE(g.node(ldelem).is_effect());
}

TEST(CSharpNodesTest, NewArrIsEffectful) {
    Graph g;
    auto len = g.create_const_int(10);
    NodeId inputs[] = {len};
    auto newarr = g.create(NodeKind::NewArr, inputs);
    EXPECT_TRUE(g.node(newarr).is_effect());
}

TEST(CSharpNodesTest, ConversionNodesArePure) {
    Graph g;
    auto i = g.create_const_int(42);
    NodeId inputs[] = {i};
    auto conv_i1 = g.create(NodeKind::ConvI1, inputs);
    auto conv_r8 = g.create(NodeKind::ConvR8, inputs);
    EXPECT_TRUE(g.node(conv_i1).is_pure());
    EXPECT_TRUE(g.node(conv_r8).is_pure());
}

TEST(CSharpNodesTest, OverflowConversionNodesAreEffectfulAndGuarded) {
    Graph g;
    auto i = g.create_const_int(42);
    NodeId inputs[] = {i};
    auto conv_ovf = g.create(NodeKind::ConvOvfI4, inputs);
    EXPECT_TRUE(g.node(conv_ovf).is_effect());
    EXPECT_TRUE(g.node(conv_ovf).is_guard());  // can throw → requires FrameState
}

TEST(CSharpNodesTest, LdLocAndStLocArePureVsEffectful) {
    Graph g;
    auto ld = g.create(NodeKind::LdLoc);
    auto st = g.create(NodeKind::StLoc);
    EXPECT_TRUE(g.node(ld).is_pure());
    EXPECT_FALSE(g.node(ld).is_effect());
    EXPECT_FALSE(g.node(st).is_pure());
    // StLoc stores but has no data input (operand from stack) in our model —
    // it just writes. We treat it as effectful because it changes runtime state.
}

TEST(CSharpNodesTest, VerifierCatchesMissingEffectInputOnBox) {
    Graph g;
    auto start = g.create(NodeKind::Start);
    auto i = g.create_const_int(42);
    NodeId inputs[] = {i};
    auto box = g.create(NodeKind::Box, inputs);  // no effect input
    (void)box;
    auto r = verify_graph(g);
    EXPECT_FALSE(r.has_value());
    EXPECT_NE(r.error().what().find("effect"), std::string::npos);
    (void)start;
}

TEST(CSharpNodesTest, VerifierPassesWhenBoxHasEffectInput) {
    Graph g;
    auto start = g.create(NodeKind::Start);
    auto i = g.create_const_int(42);
    NodeId inputs[] = {i};
    auto box = g.create(NodeKind::Box, inputs);
    g.set_effect_input(box, start);
    auto r = verify_graph(g);
    EXPECT_TRUE(r.has_value()) << r.error().what();
}

TEST(CSharpNodesTest, NodeKindNameForCSharpKindsIsHumanReadable) {
    EXPECT_EQ(node_kind_name(NodeKind::Box), "Box");
    EXPECT_EQ(node_kind_name(NodeKind::Unbox), "Unbox");
    EXPECT_EQ(node_kind_name(NodeKind::CastClass), "CastClass");
    EXPECT_EQ(node_kind_name(NodeKind::IsInst), "IsInst");
    EXPECT_EQ(node_kind_name(NodeKind::NewObj), "NewObj");
    EXPECT_EQ(node_kind_name(NodeKind::CallVirt), "CallVirt");
    EXPECT_EQ(node_kind_name(NodeKind::Constrained), "Constrained");
    EXPECT_EQ(node_kind_name(NodeKind::LdFld), "LdFld");
    EXPECT_EQ(node_kind_name(NodeKind::StFld), "StFld");
    EXPECT_EQ(node_kind_name(NodeKind::Throw), "Throw");
    EXPECT_EQ(node_kind_name(NodeKind::Leave), "Leave");
    EXPECT_EQ(node_kind_name(NodeKind::ConvI4), "ConvI4");
    EXPECT_EQ(node_kind_name(NodeKind::ConvOvfI4), "ConvOvfI4");
}
