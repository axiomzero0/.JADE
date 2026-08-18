// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_ir_verifier.cpp

#include <gtest/gtest.h>
#include "jade/ir/Graph.hpp"
#include "jade/ir/Verifier.hpp"
#include "jade/ir/NodeKind.hpp"

using namespace jade;

TEST(VerifierTest, EmptyGraphPasses) {
    Graph g;
    auto r = verify_graph(g);
    EXPECT_TRUE(r.has_value());
}

TEST(VerifierTest, SimpleValidGraphPasses) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto i = b.const_int(42);
    auto ret = b.return_node(i);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    auto r = verify_graph(g);
    EXPECT_TRUE(r.has_value()) << r.error().what();
}

TEST(VerifierTest, EffectfulNodeWithoutEffectInputFails) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    // Allocate is effectful but we don't wire up its effect input.
    auto alloc = b.allocate(ShapeId{1});
    (void)alloc;
    auto r = verify_graph(g);
    EXPECT_FALSE(r.has_value());
    EXPECT_NE(r.error().what().find("effect"), std::string::npos);
}

TEST(VerifierTest, PureNodeWithEffectInputFails) {
    Graph g;
    auto start = g.create(NodeKind::Start);
    auto a = g.create_const_int(1);
    auto b = g.create_const_int(2);
    NodeId inputs[] = {a, b};
    auto add = g.create(NodeKind::Add, inputs);
    // Pure node should not have effect input — wire one up anyway to fail.
    g.set_effect_input(add, start);
    auto r = verify_graph(g);
    EXPECT_FALSE(r.has_value());
    EXPECT_NE(r.error().what().find("Pure"), std::string::npos);
}

TEST(VerifierTest, GuardWithoutFrameStateFails) {
    Graph g;
    auto start = g.create(NodeKind::Start);
    auto i = g.create_const_int(0);
    NodeId inputs[] = {i};
    auto check = g.create(NodeKind::CheckInt, inputs);
    g.set_ctrl_input(check, start);
    auto r = verify_graph(g);
    EXPECT_FALSE(r.has_value());
    EXPECT_NE(r.error().what().find("Guard"), std::string::npos);
}

TEST(VerifierTest, GuardWithFrameStatePasses) {
    Graph g;
    auto start = g.create(NodeKind::Start);
    auto i = g.create_const_int(0);
    NodeId inputs[] = {i};
    auto check = g.create(NodeKind::CheckInt, inputs);
    g.set_ctrl_input(check, start);
    g.set_frame_state(check, FrameStateId{1});
    auto r = verify_graph(g);
    EXPECT_TRUE(r.has_value()) << r.error().what();
}

TEST(VerifierTest, DeadNodeReferencedByLiveNodeFails) {
    Graph g;
    auto a = g.create_const_int(1);
    auto b = g.create_const_int(2);
    NodeId inputs[] = {a, b};
    auto add = g.create(NodeKind::Add, inputs);
    // Mark `a` dead, but `add` still references it.
    g.mark_dead(a);
    auto r = verify_graph(g);
    EXPECT_FALSE(r.has_value());
    EXPECT_NE(r.error().what().find("IsDead"), std::string::npos);
    (void)add;
}

TEST(VerifierTest, VerifyIfEnabledPassesWhenDisabled) {
    // In release builds (NDEBUG), the verifier is skipped.
    // In debug builds it runs.
    Graph g;
    auto r = verify_if_enabled(g);
    EXPECT_TRUE(r.has_value());
}

TEST(VerifierTest, MultipleErrorsAreAllReported) {
    Graph g;
    // Two effectful nodes without effect input
    g.create(NodeKind::Allocate);
    g.create(NodeKind::Allocate);
    auto r = verify_graph(g);
    EXPECT_FALSE(r.has_value());
    // Both should be reported
    const auto msg = std::string{r.error().what()};
    EXPECT_NE(msg.find("%1"), std::string::npos);
    EXPECT_NE(msg.find("%2"), std::string::npos);
}

TEST(VerifierTest, VerifierErrorsHaveVerificationFailedKind) {
    Graph g;
    g.create(NodeKind::Allocate);  // no effect input
    auto r = verify_graph(g);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().kind, ErrorKind::VerificationFailed);
}
