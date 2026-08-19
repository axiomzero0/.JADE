// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_tier2_ruby_passes.cpp
//
// Tests for Tier 2 (RUBY) optimization passes:
//   SCCP, CSE, AlgebraicSimplification, ControlFlowSimplification,
//   LICM, GCM, BCE, NCE, EscapeAnalysis.

#include <gtest/gtest.h>
#include "jade/ir/Graph.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"
#include "jade/ir/passes/SCCP.hpp"
#include "jade/ir/passes/CSE.hpp"
#include "jade/ir/passes/AlgebraicSimplification.hpp"
#include "jade/ir/passes/ControlFlowSimplification.hpp"
#include "jade/ir/passes/LICM.hpp"
#include "jade/ir/passes/GCM.hpp"
#include "jade/ir/passes/BCE.hpp"
#include "jade/ir/passes/NCE.hpp"
#include "jade/ir/passes/EscapeAnalysis.hpp"
#include "jade/ir/passes/PassPipeline.hpp"

using namespace jade;

// ── SCCP ─────────────────────────────────────────────────────────────────────

TEST(SCPPTest, PropagatesConstantThroughAdd) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add = b.add(a, c);   // 3 + 4 = 7
    SCCPPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(add).is_const());
    EXPECT_EQ(g.side(add).const_value.i64, 7);
}

TEST(SCPPTest, PropagatesThroughChainedOps) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(10);
    auto c = b.const_int(3);
    auto sub = b.sub(a, c);    // 10 - 3 = 7
    auto d = b.const_int(2);
    auto mul = b.mul(sub, d);  // 7 * 2 = 14
    SCCPPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(sub).is_const());
    EXPECT_EQ(g.side(sub).const_value.i64, 7);
    EXPECT_TRUE(g.node(mul).is_const());
    EXPECT_EQ(g.side(mul).const_value.i64, 14);
}

TEST(SCPPTest, DoesNotPropagateWithUnknownInput) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(3);
    auto phi = g.create(NodeKind::Phi);  // unknown value
    NodeId inputs[] = {a, phi};
    auto add = g.create(NodeKind::Add, inputs);
    SCCPPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(add).is_const());
}

TEST(SCPPTest, IsIdempotent) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(5);
    auto c = b.const_int(7);
    auto add = b.add(a, c);
    SCCPPass pass;
    PassContext ctx;
    ASSERT_TRUE(pass.run(g, ctx).has_value());
    auto dump1 = g.dump();
    ASSERT_TRUE(pass.run(g, ctx).has_value());
    auto dump2 = g.dump();
    EXPECT_EQ(dump1, dump2);
    (void)add;
}

// ── CSE ──────────────────────────────────────────────────────────────────────

TEST(CSETest, DeduplicatesIdenticalPureNodes) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add1 = b.add(a, c);
    auto add2 = b.add(a, c);   // identical
    CSEPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(add1).is_dead());
    EXPECT_TRUE(g.node(add2).is_dead());
}

TEST(CSETest, DoesNotDedupeDifferentNodes) {
    Graph g;
    GraphBuilder b(g);
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto e = b.const_int(5);
    auto add1 = b.add(a, c);
    auto add2 = b.add(a, e);
    CSEPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(add1).is_dead());
    EXPECT_FALSE(g.node(add2).is_dead());
}

// ── AlgebraicSimplification ───────────────────────────────────────────────────

TEST(AlgebraicSimplificationTest, XPlusZeroIsMarked) {
    Graph g;
    GraphBuilder b(g);
    auto x = b.const_int(42);
    auto zero = b.const_int(0);
    auto add = b.add(x, zero);   // x + 0 → x
    AlgebraicSimplificationPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // The Add should be marked as const (the simplifier sets the marker).
    EXPECT_TRUE(g.node(add).is_const());
}

TEST(AlgebraicSimplificationTest, XTimesZeroIsZero) {
    Graph g;
    GraphBuilder b(g);
    auto x = b.const_int(42);
    auto zero = b.const_int(0);
    auto mul = b.mul(x, zero);   // x * 0 → 0
    AlgebraicSimplificationPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(mul).is_const());
    EXPECT_EQ(g.side(mul).const_value.i64, 0);
}

TEST(AlgebraicSimplificationTest, XTimesOneIsMarked) {
    Graph g;
    GraphBuilder b(g);
    auto x = b.const_int(42);
    auto one = b.const_int(1);
    auto mul = b.mul(x, one);    // x * 1 → x
    AlgebraicSimplificationPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(mul).is_const());
}

TEST(AlgebraicSimplificationTest, XXorXIsZero) {
    Graph g;
    GraphBuilder b(g);
    auto x = b.const_int(42);
    NodeId inputs[] = {x, x};
    auto xor_node = g.create(NodeKind::Xor, inputs);   // x ^ x → 0
    AlgebraicSimplificationPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(xor_node).is_const());
    EXPECT_EQ(g.side(xor_node).const_value.i64, 0);
}

// ── ControlFlowSimplification ─────────────────────────────────────────────────

TEST(ControlFlowSimplificationTest, DeadIfWithConstCondIsMarkedDead) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto cond = b.const_int(1);   // truthy
    auto if_node = b.if_node(cond);
    g.set_ctrl_input(if_node, start);
    ControlFlowSimplificationPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(if_node).is_dead());
}

// ── LICM ─────────────────────────────────────────────────────────────────────

TEST(LICMTest, IsNoOpOnLinearGraph) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add = b.add(a, c);
    auto ret = b.return_node(add);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    LICMPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // LICM is a no-op on linear graphs.
    EXPECT_FALSE(g.node(add).is_dead());
}

// ── GCM ─────────────────────────────────────────────────────────────────────

TEST(GCMTest, IsNoOpOnCurrentIR) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add = b.add(a, c);
    auto ret = b.return_node(add);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    GCMPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // GCM is a no-op on the current IR shape (no block structure).
    EXPECT_FALSE(g.node(add).is_dead());
}

// ── BCE ────────────────────────────────────────────────────────────────────

TEST(BCETest, EliminatesProvenInBoundsCheck) {
    Graph g;
    GraphBuilder b(g);
    auto idx = b.const_int(3);
    auto len = b.const_int(10);
    NodeId inputs[] = {idx, len};
    auto check = g.create(NodeKind::CheckBounds, inputs);   // 0 <= 3 < 10 → ok
    g.set_frame_state(check, FrameStateId{1});
    BCEPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(check).is_dead());
}

TEST(BCETest, DoesNotEliminateOutOfRangeCheck) {
    Graph g;
    GraphBuilder b(g);
    auto idx = b.const_int(15);
    auto len = b.const_int(10);
    NodeId inputs[] = {idx, len};
    auto check = g.create(NodeKind::CheckBounds, inputs);   // 15 >= 10 → fail
    g.set_frame_state(check, FrameStateId{1});
    BCEPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(check).is_dead());
}

TEST(BCETest, DoesNotEliminateNegativeIndexCheck) {
    Graph g;
    GraphBuilder b(g);
    auto idx = b.const_int(-1);
    auto len = b.const_int(10);
    NodeId inputs[] = {idx, len};
    auto check = g.create(NodeKind::CheckBounds, inputs);   // -1 < 0 → fail
    g.set_frame_state(check, FrameStateId{1});
    BCEPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(check).is_dead());
}

// ── NCE ──────────────────────────────────────────────────────────────────────

TEST(NCETest, EliminatesCheckNotNullOnNewObj) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto new_obj = g.create(NodeKind::NewObj);
    g.set_effect_input(new_obj, start);
    NodeId inputs[] = {new_obj};
    auto check = g.create(NodeKind::CheckNotNull, inputs);
    g.set_frame_state(check, FrameStateId{1});
    NCEPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(check).is_dead());
}

TEST(NCETest, DoesNotEliminateCheckOnUnknownValue) {
    Graph g;
    GraphBuilder b(g);
    auto ld = g.create(NodeKind::LdLoc);   // unknown provenance
    NodeId inputs[] = {ld};
    auto check = g.create(NodeKind::CheckNotNull, inputs);
    g.set_frame_state(check, FrameStateId{1});
    NCEPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(check).is_dead());
}

TEST(NCETest, EliminatesCheckOnPreviousCheckResult) {
    Graph g;
    GraphBuilder b(g);
    auto ld = g.create(NodeKind::LdLoc);
    NodeId inputs1[] = {ld};
    auto check1 = g.create(NodeKind::CheckNotNull, inputs1);
    g.set_frame_state(check1, FrameStateId{1});
    NodeId inputs2[] = {check1};
    auto check2 = g.create(NodeKind::CheckNotNull, inputs2);   // redundant
    g.set_frame_state(check2, FrameStateId{1});
    NCEPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(check1).is_dead());
    EXPECT_TRUE(g.node(check2).is_dead());
}

// ── EscapeAnalysis ────────────────────────────────────────────────────────────

TEST(EscapeAnalysisTest, EliminatesNonEscapingAllocate) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    // No uses → non-escaping.
    EscapeAnalysisPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(alloc).is_dead());
}

TEST(EscapeAnalysisTest, DoesNotEliminateEscapingAllocate) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto ret = b.return_node(alloc);   // escapes via return
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, alloc);
    EscapeAnalysisPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(alloc).is_dead());
}

TEST(EscapeAnalysisTest, DoesNotEliminateAllocatePassedToCall) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    NodeId inputs[] = {alloc};
    auto call = g.create(NodeKind::Call, inputs);
    g.set_effect_input(call, alloc);
    EscapeAnalysisPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(alloc).is_dead());
}

TEST(EscapeAnalysisTest, DoesNotEliminateAllocateUsedByLdFld) {
    // An allocation that is used by LdFld is "non-escaping" in the strict
    // sense, but we cannot eliminate it without SRA (Scalar Replacement
    // of Aggregates), which is a DIAMOND-tier pass. The basic EA pass
    // conservatively keeps the allocation alive when it has any non-dead
    // use, because marking it dead would violate the verifier (Rule 42:
    // no dead nodes with live users).
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto lf = b.load_field(alloc, StringId{1}, 8);
    g.set_effect_input(lf, alloc);
    EscapeAnalysisPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // Without SRA, EA cannot eliminate the allocation — it has a live use.
    EXPECT_FALSE(g.node(alloc).is_dead());
}

// ── Full RUBY pipeline ─────────────────────────────────────────────────────────

TEST(RubyPipelineTest, RunsAllNewPassesWithoutCrash) {
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
    auto pipe = build_ruby_pipeline();
    auto r = pipe->run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // The pipeline should not crash and should leave the graph in a valid state.
    EXPECT_TRUE(verify_graph(g).has_value());
}

TEST(RubyPipelineTest, FoldsConstantArithmeticToSingleValue) {
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
    auto pipe = build_ruby_pipeline();
    auto r = pipe->run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // The Mul should be folded to ConstInt(35) by ConstantFolding (after
    // SCCP marks the Add as const 7).
    EXPECT_TRUE(g.node(mul).is_const());
    EXPECT_EQ(g.side(mul).const_value.i64, 35);
}
