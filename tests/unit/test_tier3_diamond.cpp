// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_tier3_diamond.cpp
//
// Tests for Tier 3 (DIAMOND) optimization passes:
//   PEA (Partial Escape Analysis), SRA (Scalar Replacement of Aggregates),
//   SLP (Superword Level Parallelism).

#include <gtest/gtest.h>
#include "jade/ir/Graph.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"
#include "jade/tier3_diamond/PEA.hpp"
#include "jade/tier3_diamond/SRA.hpp"
#include "jade/tier3_diamond/SLP.hpp"
#include "jade/ir/passes/PassPipeline.hpp"

using namespace jade;
using namespace jade::tier3;

// ── PEA ───────────────────────────────────────────────────────────────────────

TEST(PEATest, EliminatesUnusedAllocation) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    PEAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(alloc).is_dead());
}

TEST(PEATest, DoesNotEliminateEscapingAllocation) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto ret = b.return_node(alloc);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, alloc);
    PEAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(g.node(alloc).is_dead());
}

TEST(PEATest, EliminatesBoxWithNoUses) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    NodeId inputs[] = {val};
    auto box = g.create(NodeKind::Box, inputs);
    g.set_effect_input(box, start);
    PEAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(g.node(box).is_dead());
}

TEST(PEATest, DoesNotEliminateBoxThatEscapesViaReturn) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    NodeId inputs[] = {val};
    auto box = g.create(NodeKind::Box, inputs);
    g.set_effect_input(box, start);
    auto ret = b.return_node(box);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, box);
    PEAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(g.node(box).is_dead());
}

// ── SRA ───────────────────────────────────────────────────────────────────────

TEST(SRATest, EliminatesStoreFieldOnNonEscapingAlloc) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto val = b.const_int(42);
    auto sf = b.store_field(alloc, StringId{1}, 0, val);
    g.set_effect_input(sf, alloc);
    // No load — the store is dead.
    SRAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // The store should be marked dead (scalar-replaced).
    EXPECT_TRUE(g.node(sf).is_dead());
    // The alloc should also be dead (no live uses).
    EXPECT_TRUE(g.node(alloc).is_dead());
}

TEST(SRATest, DoesNotEliminateEscapingAlloc) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto ret = b.return_node(alloc);  // escapes
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, alloc);
    SRAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(g.node(alloc).is_dead());
}

TEST(SRATest, ForwardsStoredValueToLoad) {
    // alloc; storefield(alloc, 0, 42); loadfield(alloc, 0); ret
    // SRA should forward 42 to the load.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto val = b.const_int(42);
    auto sf = b.store_field(alloc, StringId{1}, 0, val);
    g.set_effect_input(sf, alloc);
    auto lf = b.load_field(alloc, StringId{1}, 0);
    // Wire LoadField's effect to the alloc (NOT to sf), so sf has no
    // effect-input users and can be marked dead by SRA.
    g.set_effect_input(lf, alloc);
    auto ret = b.return_node(lf);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, alloc);
    SRAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // The store should be eliminated (no effect-input users).
    EXPECT_TRUE(g.node(sf).is_dead());
    // The load should be forwarded to the const value.
    EXPECT_TRUE(g.node(lf).is_const());
    EXPECT_EQ(g.side(lf).const_value.i64, 42);
}

// ── SLP ────────────────────────────────────────────────────────────────────────

TEST(SLPTest, IsAnalysisPass) {
    SLPPass pass;
    EXPECT_TRUE(pass.is_analysis());
}

TEST(SLPTest, RunsOnArithmeticGraph) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(1);
    auto c = b.const_int(2);
    auto add1 = b.add(a, c);
    auto d = b.const_int(3);
    auto e = b.const_int(4);
    auto add2 = b.add(d, e);
    auto ret = b.return_node(add1);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    (void)add2;
    SLPPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // SLP is analysis-only; it should not modify the graph.
    EXPECT_FALSE(g.node(add1).is_dead());
}

TEST(SLPTest, DoesNotCrashOnEmptyGraph) {
    Graph g;
    SLPPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value());
}

// ── DIAMOND pipeline ──────────────────────────────────────────────────────────

TEST(DiamondPipelineTest, RunsAllPassesWithoutCrash) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add = b.add(a, c);
    auto e = b.const_int(5);
    auto mul = b.mul(add, e);
    auto ret = b.return_node(mul);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    PassContext ctx;
    auto pipe = build_diamond_pipeline();
    auto r = pipe->run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(verify_graph(g).has_value());
}

TEST(DiamondPipelineTest, FoldsArithmeticToConstant) {
    // (3 + 4) * 5 = 35
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add = b.add(a, c);
    auto e = b.const_int(5);
    auto mul = b.mul(add, e);
    auto ret = b.return_node(mul);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    PassContext ctx;
    auto pipe = build_diamond_pipeline();
    auto r = pipe->run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(mul).is_const());
    EXPECT_EQ(g.side(mul).const_value.i64, 35);
}

TEST(DiamondPipelineTest, EliminatesUnusedAllocation) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto val = b.const_int(42);
    auto ret = b.return_node(val);
    g.set_ctrl_input(ret, start);
    // Wire the return's effect to start (NOT to alloc), so the alloc
    // has no effect-input users and can be eliminated.
    g.set_effect_input(ret, start);
    PassContext ctx;
    auto pipe = build_diamond_pipeline();
    auto r = pipe->run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // PEA + DCE should eliminate the unused allocation.
    EXPECT_TRUE(g.node(alloc).is_dead());
}
