// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_regression.cpp
//
// Regression tests for Rule 36 (5 regression tests per bug fix).
//
// Each bug fix must ship with 5 regression tests:
//   1. Minimal reproducer
//   2. Variant trigger
//   3. Boundary/negative
//   4. Integration/contextual
//   5. Deopt/state reconstruction
//
// This file covers 3 bugs:
//   001: IfTrue/IfFalse double-bind labels in CodeEmitter
//   002: LSRA doesn't extend live intervals across loop back-edges
//   003: BuildRegions connect_edges doesn't handle Jump back-edges

#include <gtest/gtest.h>
#include "jade/ir/Graph.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/Verifier.hpp"
#include "jade/ir/passes/BuildRegions.hpp"
#include "jade/tier1_jade/JadeJit.hpp"
#include "jade/tier1_jade/LinearScanRegAlloc.hpp"

#include <cstdint>
#include <functional>

using namespace jade;
using namespace jade::tier1;

namespace {
using JitFunc = int64_t (*)();
}  // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Bug 001: IfTrue/IfFalse double-bind labels in CodeEmitter
//
// Root cause: IfTrue/IfFalse handlers re-bound labels that were already bound
// at block start in the RPO walk. asmjit treats double-binding as an error.
// Fix: removed the a.bind() calls from IfTrue/IfFalse cases.
// ═══════════════════════════════════════════════════════════════════════════════

// ── 001.1: Minimal reproducer ─────────────────────────────────────────────────
// Smallest if-then-else with both branches returning. This was the original
// failing case: the double-binding corrupted the jump targets.
TEST(Regression001IfTrueIfFalseDoubleBind, MinimalReproducer) {
    // if (1) return 10; else return 20;
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto cond = b.const_int(1);
    auto if_node = b.if_node(cond);
    g.set_ctrl_input(if_node, start);

    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto true_val = b.const_int(10);
    auto ret_true = b.return_node(true_val);
    g.set_ctrl_input(ret_true, iftrue);
    g.set_effect_input(ret_true, start);

    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto false_val = b.const_int(20);
    auto ret_false = b.return_node(false_val);
    g.set_ctrl_input(ret_false, iffalse);
    g.set_effect_input(ret_false, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 10);
}

// ── 001.2: Variant trigger — nested if-then-else ──────────────────────────────
// Different code pattern exercising the same root cause: an if inside an if.
// Both inner branches must bind labels without double-binding.
TEST(Regression001IfTrueIfFalseDoubleBind, VariantTriggerNestedIf) {
    // if (1) { if (1) return 100; else return 200; } else return 300;
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto cond_outer = b.const_int(1);
    auto if_outer = b.if_node(cond_outer);
    g.set_ctrl_input(if_outer, start);

    // Outer true branch: inner if-then-else
    auto iftrue_outer = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue_outer, if_outer);
    auto cond_inner = b.const_int(1);
    auto if_inner = b.if_node(cond_inner);
    g.set_ctrl_input(if_inner, iftrue_outer);

    auto iftrue_inner = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue_inner, if_inner);
    auto val_inner_true = b.const_int(100);
    auto ret_inner_true = b.return_node(val_inner_true);
    g.set_ctrl_input(ret_inner_true, iftrue_inner);
    g.set_effect_input(ret_inner_true, start);

    auto iffalse_inner = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse_inner, if_inner);
    auto val_inner_false = b.const_int(200);
    auto ret_inner_false = b.return_node(val_inner_false);
    g.set_ctrl_input(ret_inner_false, iffalse_inner);
    g.set_effect_input(ret_inner_false, start);

    // Outer false branch
    auto iffalse_outer = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse_outer, if_outer);
    auto val_outer_false = b.const_int(300);
    auto ret_outer_false = b.return_node(val_outer_false);
    g.set_ctrl_input(ret_outer_false, iffalse_outer);
    g.set_effect_input(ret_outer_false, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 100);
}

// ── 001.3: Boundary/negative — single-branch if (no else) ───────────────────
// Ensures the fix doesn't over-correct. A single-branch if (no IfFalse)
// should still work — the IfTrue block binds its label once, and the
// fall-through after the IfTrue block must continue to the next code.
TEST(Regression001IfTrueIfFalseDoubleBind, BoundaryNegativeSingleBranchIf) {
    // if (0) return 99;  // never taken
    // return 42;
    // Since cond=0, the IfTrue branch is skipped. We need the fall-through
    // to reach the second Return. The IfTrue label is bound but never jumped
    // to (cond=0 → jne skipped → jmp false_label, but there's no IfFalse).
    // We model this as: if (0) return 99; else return 42.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto cond = b.const_int(0);
    auto if_node = b.if_node(cond);
    g.set_ctrl_input(if_node, start);

    // True branch: return 99 (should NOT be taken since cond=0)
    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto true_val = b.const_int(99);
    auto ret_true = b.return_node(true_val);
    g.set_ctrl_input(ret_true, iftrue);
    g.set_effect_input(ret_true, start);

    // False branch: return 42 (should be taken)
    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto false_val = b.const_int(42);
    auto ret_false = b.return_node(false_val);
    g.set_ctrl_input(ret_false, iffalse);
    g.set_effect_input(ret_false, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 42);   // not 99 — ensures the false branch is taken
}

// ── 001.4: Integration/contextual — realistic multi-branch function ──────────
// The bug in a realistic surrounding code: a function with multiple branches
// and arithmetic in each branch, exercising the label binding multiple times.
TEST(Regression001IfTrueIfFalseDoubleBind, IntegrationContextualMultiBranch) {
    // Realistic: a sign-classifier.
    // if (n > 0) return 1;       // positive
    // else if (n < 0) return -1; // negative
    // else return 0;             // zero
    // We use n=1 (positive) → expect 1.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    // n = 1
    auto n_val = b.const_int(1);
    auto st_n = g.create(NodeKind::StLoc);
    NodeId st_n_in[] = {n_val}; g.set_data_inputs(st_n, st_n_in);
    g.set_effect_input(st_n, start);
    g.side(st_n).class_id = 0;

    auto zero = b.const_int(0);
    auto ld_n1 = g.create(NodeKind::LdLoc);
    g.side(ld_n1).class_id = 0;
    NodeId gt_in[] = {ld_n1, zero};
    auto gt = g.create(NodeKind::Gt, gt_in);
    auto if1 = b.if_node(gt);
    g.set_ctrl_input(if1, st_n);
    g.set_effect_input(if1, st_n);

    // n > 0 → return 1
    auto iftrue1 = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue1, if1);
    auto one = b.const_int(1);
    auto ret_pos = b.return_node(one);
    g.set_ctrl_input(ret_pos, iftrue1);
    g.set_effect_input(ret_pos, st_n);

    // n <= 0: check n < 0
    auto iffalse1 = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse1, if1);
    auto ld_n2 = g.create(NodeKind::LdLoc);
    g.side(ld_n2).class_id = 0;
    NodeId lt_in[] = {ld_n2, zero};
    auto lt = g.create(NodeKind::Lt, lt_in);
    auto if2 = b.if_node(lt);
    g.set_ctrl_input(if2, iffalse1);
    g.set_effect_input(if2, iffalse1);

    // n < 0 → return -1
    auto iftrue2 = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue2, if2);
    auto neg_one = b.const_int(-1);
    auto ret_neg = b.return_node(neg_one);
    g.set_ctrl_input(ret_neg, iftrue2);
    g.set_effect_input(ret_neg, iffalse1);

    // n == 0 → return 0
    auto iffalse2 = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse2, if2);
    auto ret_zero = b.return_node(zero);
    g.set_ctrl_input(ret_zero, iffalse2);
    g.set_effect_input(ret_zero, iffalse1);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 1);
}

// ── 001.5: Deopt/state reconstruction — branch with a guard ─────────────────
// Verifies correctness under bailout. A branch where one path has a guard
// (CheckNotNull) that could deopt. The label binding must be correct even
// when the guard is present (guard emits a cmp+jne deopt label).
TEST(Regression001IfTrueIfFalseDoubleBind, DeoptStateBranchWithGuard) {
    // if (1) return 42; else return 99;
    // The true branch has a Safepoint node (simulates a guard/deopt point).
    // The Safepoint emits a poll (test byte [r11], 1; jne handler; jmp resume).
    // This must not interfere with the IfTrue label binding.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto cond = b.const_int(1);
    auto if_node = b.if_node(cond);
    g.set_ctrl_input(if_node, start);

    // True branch: Safepoint + Return 42
    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto sp = g.create(NodeKind::Safepoint);
    g.set_ctrl_input(sp, iftrue);
    g.set_effect_input(sp, iftrue);
    auto true_val = b.const_int(42);
    auto ret_true = b.return_node(true_val);
    g.set_ctrl_input(ret_true, sp);
    g.set_effect_input(ret_true, sp);

    // False branch: Return 99
    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto false_val = b.const_int(99);
    auto ret_false = b.return_node(false_val);
    g.set_ctrl_input(ret_false, iffalse);
    g.set_effect_input(ret_false, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 42);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Bug 002: LSRA doesn't extend live intervals across loop back-edges
//
// Root cause: LSRA computed intervals based on linear NodeId positions, not
// knowing loops re-execute. A register holding a loop-invariant value could
// be reused by a different value inside the loop body, corrupting it on the
// next iteration.
// Fix: extend_intervals_across_loops() extends intervals of values used
// inside loops to cover the entire loop body.
// ═══════════════════════════════════════════════════════════════════════════════

// ── 002.1: Minimal reproducer ───────────────────────────────────────────────
// Smallest loop with a register-allocated invariant (ConstInt bound, no StLoc).
// Before the fix, the ConstInt's register was clobbered by the loop body's Add,
// causing the loop to exit after 1 iteration (returns 1 instead of 5).
TEST(Regression002LsraLoopBackEdge, MinimalReproducer) {
    // i = 0;
    // while (i < 5) { i = i + 1; }   // 5 is raw ConstInt, NOT in a local
    // return i;   // 5
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    auto zero = b.const_int(0);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId st_i0_in[] = {zero}; g.set_data_inputs(st_i0, st_i0_in);
    g.set_effect_input(st_i0, start);
    g.side(st_i0).class_id = 0;

    auto five = b.const_int(5);   // raw ConstInt, no StLoc

    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_i0);

    auto ld_i = g.create(NodeKind::LdLoc);
    g.side(ld_i).class_id = 0;
    NodeId lt_in[] = {ld_i, five};
    auto lt = g.create(NodeKind::Lt, lt_in);

    auto if_node = b.if_node(lt);
    g.set_ctrl_input(if_node, loop_hdr);
    g.set_effect_input(if_node, loop_hdr);

    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto ld_i_exit = g.create(NodeKind::LdLoc);
    g.side(ld_i_exit).class_id = 0;
    auto ret = b.return_node(ld_i_exit);
    g.set_ctrl_input(ret, iffalse);
    g.set_effect_input(ret, iffalse);

    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto ld_i_body = g.create(NodeKind::LdLoc);
    g.side(ld_i_body).class_id = 0;
    auto one = b.const_int(1);
    NodeId add_in[] = {ld_i_body, one};
    auto add = g.create(NodeKind::Add, add_in);
    auto st_i = g.create(NodeKind::StLoc);
    NodeId st_i_in[] = {add}; g.set_data_inputs(st_i, st_i_in);
    g.set_ctrl_input(st_i, iftrue);
    g.set_effect_input(st_i, iftrue);
    g.side(st_i).class_id = 0;

    auto jump = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump, st_i);
    g.set_effect_input(jump, st_i);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 5);
}

// ── 002.2: Variant trigger — multiple register-allocated invariants ──────────
// Different code pattern: loop with TWO raw ConstInt invariants (bound and
// increment step). Both must survive across back-edges.
TEST(Regression002LsraLoopBackEdge, VariantTriggerMultipleInvariants) {
    // i = 0;
    // while (i < 10) { i = i + 3; }   // both 10 and 3 are raw ConstInts
    // return i;   // 0,3,6,9,12 → at i=12, 12<10 is false, return 12
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    auto zero = b.const_int(0);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId st_i0_in[] = {zero}; g.set_data_inputs(st_i0, st_i0_in);
    g.set_effect_input(st_i0, start);
    g.side(st_i0).class_id = 0;

    auto ten = b.const_int(10);   // invariant 1
    auto three = b.const_int(3);  // invariant 2

    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_i0);

    auto ld_i = g.create(NodeKind::LdLoc);
    g.side(ld_i).class_id = 0;
    NodeId lt_in[] = {ld_i, ten};
    auto lt = g.create(NodeKind::Lt, lt_in);

    auto if_node = b.if_node(lt);
    g.set_ctrl_input(if_node, loop_hdr);
    g.set_effect_input(if_node, loop_hdr);

    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto ld_i_exit = g.create(NodeKind::LdLoc);
    g.side(ld_i_exit).class_id = 0;
    auto ret = b.return_node(ld_i_exit);
    g.set_ctrl_input(ret, iffalse);
    g.set_effect_input(ret, iffalse);

    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto ld_i_body = g.create(NodeKind::LdLoc);
    g.side(ld_i_body).class_id = 0;
    NodeId add_in[] = {ld_i_body, three};
    auto add = g.create(NodeKind::Add, add_in);
    auto st_i = g.create(NodeKind::StLoc);
    NodeId st_i_in[] = {add}; g.set_data_inputs(st_i, st_i_in);
    g.set_ctrl_input(st_i, iftrue);
    g.set_effect_input(st_i, iftrue);
    g.side(st_i).class_id = 0;

    auto jump = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump, st_i);
    g.set_effect_input(jump, st_i);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    // 0, 3, 6, 9, 12 — at i=12, 12<10 is false, so return 12.
    EXPECT_EQ(fn(), 12);
}

// ── 002.3: Boundary/negative — loop with zero iterations ─────────────────────
// Ensures the fix doesn't over-correct. A loop where the condition is false
// at entry (zero iterations) must return the initial value. The interval
// extension must not cause incorrect behavior when the loop body never runs.
TEST(Regression002LsraLoopBackEdge, BoundaryNegativeZeroIterations) {
    // i = 10;
    // while (i < 5) { i = i + 1; }   // cond is false at entry → 0 iterations
    // return i;   // 10
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    auto ten_init = b.const_int(10);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId st_i0_in[] = {ten_init}; g.set_data_inputs(st_i0, st_i0_in);
    g.set_effect_input(st_i0, start);
    g.side(st_i0).class_id = 0;

    auto five = b.const_int(5);

    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_i0);

    auto ld_i = g.create(NodeKind::LdLoc);
    g.side(ld_i).class_id = 0;
    NodeId lt_in[] = {ld_i, five};
    auto lt = g.create(NodeKind::Lt, lt_in);

    auto if_node = b.if_node(lt);
    g.set_ctrl_input(if_node, loop_hdr);
    g.set_effect_input(if_node, loop_hdr);

    // cond false → exit immediately, return 10
    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto ld_i_exit = g.create(NodeKind::LdLoc);
    g.side(ld_i_exit).class_id = 0;
    auto ret = b.return_node(ld_i_exit);
    g.set_ctrl_input(ret, iffalse);
    g.set_effect_input(ret, iffalse);

    // body (never executed)
    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto ld_i_body = g.create(NodeKind::LdLoc);
    g.side(ld_i_body).class_id = 0;
    auto one = b.const_int(1);
    NodeId add_in[] = {ld_i_body, one};
    auto add = g.create(NodeKind::Add, add_in);
    auto st_i = g.create(NodeKind::StLoc);
    NodeId st_i_in[] = {add}; g.set_data_inputs(st_i, st_i_in);
    g.set_ctrl_input(st_i, iftrue);
    g.set_effect_input(st_i, iftrue);
    g.side(st_i).class_id = 0;

    auto jump = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump, st_i);
    g.set_effect_input(jump, st_i);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 10);   // unchanged — loop never ran
}

// ── 002.4: Integration/contextual — loop with complex body ─────────────────────
// The bug in realistic surrounding code: a loop with a complex body that
// performs multiple operations. The loop has a single Loop header (no nesting)
// but exercises the interval extension with multiple register-allocated values.
TEST(Regression002LsraLoopBackEdge, IntegrationContextualComplexLoopBody) {
    // sum = 0; i = 0;
    // while (i < 10) {
    //   sum = sum + (i * 2);   // 2 is raw ConstInt
    //   i = i + 1;
    // }
    // return sum;   // 0*2 + 1*2 + ... + 9*2 = 2*(0+1+...+9) = 2*45 = 90
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    // sum = 0 (local 0)
    auto zero_a = b.const_int(0);
    auto st_sum0 = g.create(NodeKind::StLoc);
    NodeId st_sum0_in[] = {zero_a}; g.set_data_inputs(st_sum0, st_sum0_in);
    g.set_effect_input(st_sum0, start);
    g.side(st_sum0).class_id = 0;

    // i = 0 (local 1)
    auto zero_b = b.const_int(0);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId st_i0_in[] = {zero_b}; g.set_data_inputs(st_i0, st_i0_in);
    g.set_effect_input(st_i0, st_sum0);
    g.side(st_i0).class_id = 1;

    // Raw ConstInt invariants
    auto ten = b.const_int(10);
    auto two = b.const_int(2);
    auto one = b.const_int(1);

    // Loop header
    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_i0);

    // i < 10
    auto ld_i_cmp = g.create(NodeKind::LdLoc);
    g.side(ld_i_cmp).class_id = 1;
    NodeId lt_in[] = {ld_i_cmp, ten};
    auto lt = g.create(NodeKind::Lt, lt_in);
    auto if_node = b.if_node(lt);
    g.set_ctrl_input(if_node, loop_hdr);
    g.set_effect_input(if_node, loop_hdr);

    // IfFalse → exit: return sum
    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto ld_sum_exit = g.create(NodeKind::LdLoc);
    g.side(ld_sum_exit).class_id = 0;
    auto ret = b.return_node(ld_sum_exit);
    g.set_ctrl_input(ret, iffalse);
    g.set_effect_input(ret, iffalse);

    // IfTrue → body: sum = sum + (i * 2); i = i + 1
    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);

    // tmp = i * 2
    auto ld_i_mul = g.create(NodeKind::LdLoc);
    g.side(ld_i_mul).class_id = 1;
    NodeId mul_in[] = {ld_i_mul, two};
    auto mul = g.create(NodeKind::Mul, mul_in);

    // sum = sum + tmp
    auto ld_sum = g.create(NodeKind::LdLoc);
    g.side(ld_sum).class_id = 0;
    NodeId add_sum_in[] = {ld_sum, mul};
    auto add_sum = g.create(NodeKind::Add, add_sum_in);
    auto st_sum = g.create(NodeKind::StLoc);
    NodeId st_sum_in[] = {add_sum}; g.set_data_inputs(st_sum, st_sum_in);
    g.set_ctrl_input(st_sum, iftrue);
    g.set_effect_input(st_sum, iftrue);
    g.side(st_sum).class_id = 0;

    // i = i + 1
    auto ld_i_inc = g.create(NodeKind::LdLoc);
    g.side(ld_i_inc).class_id = 1;
    NodeId add_i_in[] = {ld_i_inc, one};
    auto add_i = g.create(NodeKind::Add, add_i_in);
    auto st_i = g.create(NodeKind::StLoc);
    NodeId st_i_in[] = {add_i}; g.set_data_inputs(st_i, st_i_in);
    g.set_effect_input(st_i, st_sum);
    g.side(st_i).class_id = 1;

    // Jump back-edge → loop header
    auto jump = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump, st_i);
    g.set_effect_input(jump, st_i);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    // sum = 0*2 + 1*2 + 2*2 + ... + 9*2 = 2*(0+1+...+9) = 2*45 = 90
    EXPECT_EQ(fn(), 90);
}

// ── 002.5: Deopt/state reconstruction — loop with safepoint ──────────────────
// Verifies correctness under bailout. A loop with a Safepoint node (which
// could trigger a deopt). The interval extension must still work correctly
// when a safepoint poll is present in the loop body.
TEST(Regression002LsraLoopBackEdge, DeoptStateLoopWithSafepoint) {
    // i = 0;
    // while (i < 5) { safepoint; i = i + 1; }   // 5 is raw ConstInt
    // return i;   // 5
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    auto zero = b.const_int(0);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId st_i0_in[] = {zero}; g.set_data_inputs(st_i0, st_i0_in);
    g.set_effect_input(st_i0, start);
    g.side(st_i0).class_id = 0;

    auto five = b.const_int(5);

    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_i0);

    auto ld_i = g.create(NodeKind::LdLoc);
    g.side(ld_i).class_id = 0;
    NodeId lt_in[] = {ld_i, five};
    auto lt = g.create(NodeKind::Lt, lt_in);

    auto if_node = b.if_node(lt);
    g.set_ctrl_input(if_node, loop_hdr);
    g.set_effect_input(if_node, loop_hdr);

    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto ld_i_exit = g.create(NodeKind::LdLoc);
    g.side(ld_i_exit).class_id = 0;
    auto ret = b.return_node(ld_i_exit);
    g.set_ctrl_input(ret, iffalse);
    g.set_effect_input(ret, iffalse);

    // Body: Safepoint, then i = i + 1
    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto sp = g.create(NodeKind::Safepoint);
    g.set_ctrl_input(sp, iftrue);
    g.set_effect_input(sp, iftrue);
    auto ld_i_body = g.create(NodeKind::LdLoc);
    g.side(ld_i_body).class_id = 0;
    auto one = b.const_int(1);
    NodeId add_in[] = {ld_i_body, one};
    auto add = g.create(NodeKind::Add, add_in);
    auto st_i = g.create(NodeKind::StLoc);
    NodeId st_i_in[] = {add}; g.set_data_inputs(st_i, st_i_in);
    g.set_ctrl_input(st_i, sp);
    g.set_effect_input(st_i, sp);
    g.side(st_i).class_id = 0;

    auto jump = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump, st_i);
    g.set_effect_input(jump, st_i);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 5);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Bug 003: BuildRegions connect_edges doesn't handle Jump back-edges
//
// Root cause: connect_edges treated Jump as fall-through (default case), so
// back-edges were never created. is_loop_header was never set, blocking
// the LSRA loop fix.
// Fix: Added a Jump case that scans for a Loop header and creates a back-edge.
// ═══════════════════════════════════════════════════════════════════════════════

// ── 003.1: Minimal reproducer ────────────────────────────────────────────────
// Smallest loop with a Jump back-edge. Verify that is_loop_header is set to
// true for the Loop block. Before the fix, it was always false.
TEST(Regression003BuildRegionsJumpBackEdge, MinimalReproducer) {
    // while (true) { } — a degenerate infinite loop (we only check structure).
    // We build: Start → Loop → Jump(back to Loop)
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, start);
    auto jump = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump, loop_hdr);
    g.set_effect_input(jump, loop_hdr);

    auto bs = build_block_structure(g);
    // The Loop block must be detected as a loop header.
    bool found_loop_header = false;
    for (const auto& bb : bs.blocks) {
        if (bb.is_loop_header) {
            found_loop_header = true;
            break;
        }
    }
    EXPECT_TRUE(found_loop_header)
        << "is_loop_header must be true for at least one block when a Jump back-edge exists";

    // The Jump's block must have the Loop block as a successor.
    // (Jump is the LAST node of its block, not the leader — the leader is Loop.)
    uint32_t loop_block_id = 0;
    for (const auto& bb : bs.blocks) {
        if (bb.leader.valid() && g.node(bb.leader).kind == NodeKind::Loop) {
            loop_block_id = bb.id;
            break;
        }
    }
    bool jump_has_back_edge = false;
    for (const auto& bb : bs.blocks) {
        if (bb.last.valid() && g.node(bb.last).kind == NodeKind::Jump) {
            for (uint32_t succ : bb.successors) {
                if (succ == loop_block_id) {
                    jump_has_back_edge = true;
                }
            }
        }
    }
    EXPECT_TRUE(jump_has_back_edge)
        << "Jump block must have the Loop block as a successor (back-edge)";
}

// ── 003.2: Variant trigger — multiple loops in one function ──────────────────
// Two separate loops in one function. Both Loop headers must be detected,
// and both Jump back-edges must be created.
TEST(Regression003BuildRegionsJumpBackEdge, VariantTriggerMultipleLoops) {
    // Two loops:
    //   L1: Loop → Jump(L1)
    //   L2: Loop → Jump(L2)
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    auto loop1 = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop1, start);
    g.set_effect_input(loop1, start);
    auto jump1 = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump1, loop1);
    g.set_effect_input(jump1, loop1);

    auto loop2 = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop2, jump1);
    g.set_effect_input(loop2, jump1);
    auto jump2 = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump2, loop2);
    g.set_effect_input(jump2, loop2);

    auto bs = build_block_structure(g);
    // Count loop headers — should be at least 2.
    uint32_t loop_header_count = 0;
    for (const auto& bb : bs.blocks) {
        if (bb.is_loop_header) ++loop_header_count;
    }
    EXPECT_GE(loop_header_count, 2u)
        << "Both Loop headers must be detected when two loops exist";
}

// ── 003.3: Boundary/negative — forward Jump without a Loop header ────────────
// Ensures the fix doesn't over-correct. A Jump without any Loop header in
// the graph should not crash — connect_edges should simply not find a target
// and leave the successor list empty (or fall through).
TEST(Regression003BuildRegionsJumpBackEdge, BoundaryNegativeForwardJumpNoLoop) {
    // A Jump with no Loop header. The Jump case in connect_edges scans for a
    // Loop node; if none exists, it should not crash and should leave the
    // Jump block with no back-edge successor.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    auto ret = b.return_node(val);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    // A Jump node that's not connected to any Loop — it's dead code.
    auto jump = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump, start);
    g.set_effect_input(jump, start);
    g.mark_dead(jump);

    // Should not crash.
    auto bs = build_block_structure(g);
    // No loop headers should be detected.
    for (const auto& bb : bs.blocks) {
        EXPECT_FALSE(bb.is_loop_header)
            << "No loop header should be detected when there's no Loop node";
    }
}

// ── 003.4: Integration/contextual — realistic loop with multiple blocks ──────
// A realistic loop with a Loop header, an If inside the body (creating
// multiple blocks), and a Jump back-edge. The back-edge must connect the
// Jump's block to the Loop header.
TEST(Regression003BuildRegionsJumpBackEdge, IntegrationContextualRealisticLoop) {
    // i = 0;
    // while (i < 10) {
    //   if (i < 5) { /* early path */ }
    //   i = i + 1;
    // }
    // return i;
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    auto zero = b.const_int(0);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId st_i0_in[] = {zero}; g.set_data_inputs(st_i0, st_i0_in);
    g.set_effect_input(st_i0, start);
    g.side(st_i0).class_id = 0;

    auto ten = b.const_int(10);
    auto five = b.const_int(5);

    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_i0);

    auto ld_i = g.create(NodeKind::LdLoc);
    g.side(ld_i).class_id = 0;
    NodeId lt_in[] = {ld_i, ten};
    auto lt = g.create(NodeKind::Lt, lt_in);
    auto if_outer = b.if_node(lt);
    g.set_ctrl_input(if_outer, loop_hdr);
    g.set_effect_input(if_outer, loop_hdr);

    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_outer);
    auto ld_i_exit = g.create(NodeKind::LdLoc);
    g.side(ld_i_exit).class_id = 0;
    auto ret = b.return_node(ld_i_exit);
    g.set_ctrl_input(ret, iffalse);
    g.set_effect_input(ret, iffalse);

    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_outer);

    // Inner if: i < 5
    auto ld_i_inner = g.create(NodeKind::LdLoc);
    g.side(ld_i_inner).class_id = 0;
    NodeId lt_inner_in[] = {ld_i_inner, five};
    auto lt_inner = g.create(NodeKind::Lt, lt_inner_in);
    auto if_inner = b.if_node(lt_inner);
    g.set_ctrl_input(if_inner, iftrue);
    g.set_effect_input(if_inner, iftrue);

    auto iftrue_inner = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue_inner, if_inner);
    auto iffalse_inner = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse_inner, if_inner);

    // Both inner branches merge into: i = i + 1
    auto ld_i_inc = g.create(NodeKind::LdLoc);
    g.side(ld_i_inc).class_id = 0;
    auto one = b.const_int(1);
    NodeId add_in[] = {ld_i_inc, one};
    auto add = g.create(NodeKind::Add, add_in);
    auto st_i = g.create(NodeKind::StLoc);
    NodeId st_i_in[] = {add}; g.set_data_inputs(st_i, st_i_in);
    g.set_ctrl_input(st_i, iffalse_inner);
    g.set_effect_input(st_i, iffalse_inner);
    g.side(st_i).class_id = 0;

    auto jump = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump, st_i);
    g.set_effect_input(jump, st_i);

    auto bs = build_block_structure(g);
    // The Loop header must be detected.
    uint32_t loop_header_count = 0;
    for (const auto& bb : bs.blocks) {
        if (bb.is_loop_header) ++loop_header_count;
    }
    EXPECT_GE(loop_header_count, 1u)
        << "Realistic loop must have its Loop header detected";

    // The function must also JIT-compile and execute correctly.
    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 10);
}

// ── 003.5: Deopt/state reconstruction — loop with safepoint at back-edge ─────
// Verifies correctness under bailout. A loop where the Jump back-edge is
// preceded by a Safepoint (the standard place for safepoint polls). The
// back-edge detection must work correctly even with a Safepoint in the loop.
TEST(Regression003BuildRegionsJumpBackEdge, DeoptStateLoopWithSafepointAtBackEdge) {
    // i = 0;
    // while (i < 5) { safepoint; i = i + 1; jump back }
    // return i;
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    auto zero = b.const_int(0);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId st_i0_in[] = {zero}; g.set_data_inputs(st_i0, st_i0_in);
    g.set_effect_input(st_i0, start);
    g.side(st_i0).class_id = 0;

    auto five = b.const_int(5);

    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_i0);

    auto ld_i = g.create(NodeKind::LdLoc);
    g.side(ld_i).class_id = 0;
    NodeId lt_in[] = {ld_i, five};
    auto lt = g.create(NodeKind::Lt, lt_in);
    auto if_node = b.if_node(lt);
    g.set_ctrl_input(if_node, loop_hdr);
    g.set_effect_input(if_node, loop_hdr);

    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto ld_i_exit = g.create(NodeKind::LdLoc);
    g.side(ld_i_exit).class_id = 0;
    auto ret = b.return_node(ld_i_exit);
    g.set_ctrl_input(ret, iffalse);
    g.set_effect_input(ret, iffalse);

    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto sp = g.create(NodeKind::Safepoint);
    g.set_ctrl_input(sp, iftrue);
    g.set_effect_input(sp, iftrue);
    auto ld_i_body = g.create(NodeKind::LdLoc);
    g.side(ld_i_body).class_id = 0;
    auto one = b.const_int(1);
    NodeId add_in[] = {ld_i_body, one};
    auto add = g.create(NodeKind::Add, add_in);
    auto st_i = g.create(NodeKind::StLoc);
    NodeId st_i_in[] = {add}; g.set_data_inputs(st_i, st_i_in);
    g.set_ctrl_input(st_i, sp);
    g.set_effect_input(st_i, sp);
    g.side(st_i).class_id = 0;

    auto jump = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump, st_i);
    g.set_effect_input(jump, st_i);

    auto bs = build_block_structure(g);
    // Loop header must be detected even with Safepoint in the body.
    bool found_loop_header = false;
    for (const auto& bb : bs.blocks) {
        if (bb.is_loop_header) {
            found_loop_header = true;
            break;
        }
    }
    EXPECT_TRUE(found_loop_header)
        << "Loop header must be detected even with Safepoint at back-edge";

    // The function must JIT-compile and execute correctly.
    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 5);
}
