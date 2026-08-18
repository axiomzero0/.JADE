// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_ir_passes.cpp

#include <gtest/gtest.h>
#include "jade/ir/Graph.hpp"
#include "jade/ir/Verifier.hpp"
#include "jade/ir/passes/ConstantFolding.hpp"
#include "jade/ir/passes/DeadCodeElimination.hpp"
#include "jade/ir/passes/GVN.hpp"
#include "jade/ir/passes/PassPipeline.hpp"

using namespace jade;

// ── ConstantFolding ─────────────────────────────────────────────────────────

TEST(ConstantFoldingTest, FoldsConstIntAdd) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add = b.add(a, c);
    ConstantFoldingPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // The Add node should now carry an IsConst flag and the folded value.
    EXPECT_TRUE(g.node(add).is_const());
    EXPECT_EQ(g.side(add).const_value.i64, 7);
}

TEST(ConstantFoldingTest, FoldsSubMulDiv) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(20);
    auto c = b.const_int(5);
    auto sub = b.sub(a, c);
    auto mul = b.mul(sub, c);
    ConstantFoldingPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(sub).is_const());
    EXPECT_EQ(g.side(sub).const_value.i64, 15);
    EXPECT_TRUE(g.node(mul).is_const());
    EXPECT_EQ(g.side(mul).const_value.i64, 75);
}

TEST(ConstantFoldingTest, DoesNotFoldDivByZero) {
    Graph g2;
    auto x = g2.create_const_int(10);
    auto y = g2.create_const_int(0);
    NodeId inputs[] = {x, y};
    auto d = g2.create(NodeKind::Div, inputs);
    ConstantFoldingPass pass;
    PassContext ctx;
    auto r = pass.run(g2, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g2.node(d).is_const());  // NOT folded
}

TEST(ConstantFoldingTest, DoesNotFoldWithNonConstantInputs) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(3);
    // Use a Phi (treated as pure) as the "unknown" value.
    NodeId phi = g.create(NodeKind::Phi);
    NodeId inputs[] = {a, phi};
    auto add = g.create(NodeKind::Add, inputs);
    ConstantFoldingPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(add).is_const());
}

TEST(ConstantFoldingTest, PassIsIdempotent) {
    // Rule B.5: running the pass twice produces the identical IR.
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    b.add(a, c);
    ConstantFoldingPass pass;
    PassContext ctx;
    ASSERT_TRUE(pass.run(g, ctx).has_value());
    auto dump1 = g.dump();
    ASSERT_TRUE(pass.run(g, ctx).has_value());
    auto dump2 = g.dump();
    EXPECT_EQ(dump1, dump2);
}

// ── DCE ─────────────────────────────────────────────────────────────────────

TEST(DCETest, RemovesUnusedPureNode) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(1);
    auto c = b.const_int(2);
    auto add = b.add(a, c);  // never used
    DeadCodeEliminationPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(add).is_dead());
}

TEST(DCETest, KeepsEffectfulNodeWithNoDataUses) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    DeadCodeEliminationPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(alloc).is_dead());
}

TEST(DCETest, KeepsNodeUsedByAnother) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(1);
    auto c = b.const_int(2);
    auto add = b.add(a, c);
    auto ret = b.return_node(add);
    DeadCodeEliminationPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(add).is_dead());
    EXPECT_FALSE(g.node(ret).is_dead());
}

TEST(DCETest, IteratesToFixpoint) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(1);
    auto c = b.const_int(2);
    auto add1 = b.add(a, c);          // unused
    auto add2 = b.add(add1, a);       // unused; transitively dead
    DeadCodeEliminationPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(add1).is_dead());
    EXPECT_TRUE(g.node(add2).is_dead());
}

// ── GVN ─────────────────────────────────────────────────────────────────────

TEST(GVNTest, DeduplicatesIdenticalAdds) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add1 = b.add(a, c);
    auto add2 = b.add(a, c);  // identical
    GVNPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // The first occurrence stays; the second is marked dead.
    EXPECT_FALSE(g.node(add1).is_dead());
    EXPECT_TRUE(g.node(add2).is_dead());
}

TEST(GVNTest, CatchesCommutativeDuplicates) {
    // a + b should dedupe with b + a (Rule 1.6)
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add1 = b.add(a, c);
    auto add2 = b.add(c, a);  // commuted
    GVNPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(add1).is_dead());
    EXPECT_TRUE(g.node(add2).is_dead());
}

TEST(GVNTest, DoesNotDedupeDifferentNodes) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto e = b.const_int(5);
    auto add1 = b.add(a, c);
    auto add2 = b.add(a, e);  // different input
    GVNPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(add1).is_dead());
    EXPECT_FALSE(g.node(add2).is_dead());
}

// ── Pipeline ─────────────────────────────────────────────────────────────────

TEST(PipelineTest, RubyPipelineRunsAllPasses) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add1 = b.add(a, c);
    auto add2 = b.add(a, c);  // duplicate
    auto ret = b.return_node(add1);
    // Wire control and effect edges so the verifier is happy.
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    PassContext ctx;
    auto pipe = build_ruby_pipeline();
    auto r = pipe->run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // GVN should have deduplicated add2.
    EXPECT_TRUE(g.node(add2).is_dead());
    // Ret should still be live.
    EXPECT_FALSE(g.node(ret).is_dead());
    (void)add1;
}
