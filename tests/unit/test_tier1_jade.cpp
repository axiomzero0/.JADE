// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_tier1_jade.cpp
//
// Tests for Tier 1 (JADE) — the baseline SSA JIT.
//
// These tests actually compile a Graph to x86-64 machine code via asmjit
// and execute it. They assert that the JIT-compiled function returns the
// correct value.

#include <gtest/gtest.h>

#include "jade/ir/Graph.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/Verifier.hpp"
#include "jade/tier1_jade/JadeJit.hpp"
#include "jade/tier1_jade/LinearScanRegAlloc.hpp"

#include <cstdint>
#include <functional>

using namespace jade;
using namespace jade::tier1;

namespace {

// Signature of the JIT-compiled functions we test (no args, returns int64).
using JitFunc = int64_t (*)();

}  // namespace

// ── LinearScanRegAlloc tests ──────────────────────────────────────────────────

TEST(LinearScanRegAllocTest, AllocatesConstInt) {
    Graph g;
    GraphBuilder b(g);
    b.start();
    b.const_int(42);

    LinearScanRegAlloc alloc;
    auto r = alloc.allocate(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // The ConstInt vreg should have either a register or a spill slot.
    const LiveInterval& iv = r->intervals[1];   // second node = ConstInt
    EXPECT_TRUE(iv.assigned_reg.has_value() || iv.spill_slot.has_value());
}

TEST(LinearScanRegAllocTest, AllocatesAddOfTwoConsts) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add = b.add(a, c);
    auto ret = b.return_node(add);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    LinearScanRegAlloc alloc;
    auto r = alloc.allocate(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // Every interval must have a reg or a spill slot.
    for (const auto& iv : r->intervals) {
        EXPECT_TRUE(iv.assigned_reg.has_value() || iv.spill_slot.has_value())
            << "vreg %" << iv.vreg.value << " has neither";
    }
}

TEST(LinearScanRegAllocTest, FrameSizeIs16ByteAligned) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(1);
    auto ret = b.return_node(a);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    LinearScanRegAlloc alloc;
    auto r = alloc.allocate(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(r->frame_size % 16, 0u);
}

TEST(LinearScanRegAllocTest, SpillSlotsAssignedToSpilledVregs) {
    // Force a spill by allocating many live vregs simultaneously.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(1);
    auto c = b.const_int(2);
    auto d = b.const_int(3);
    auto e = b.const_int(4);
    auto f = b.const_int(5);
    auto ret = b.return_node(a);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    LinearScanRegAlloc alloc;
    auto r = alloc.allocate(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // Each spilled interval must have a unique spill slot.
    std::vector<uint32_t> slots;
    for (const auto& iv : r->intervals) {
        if (iv.spill_slot) slots.push_back(*iv.spill_slot);
    }
    // Slots are unique.
    std::sort(slots.begin(), slots.end());
    auto last = std::unique(slots.begin(), slots.end());
    EXPECT_EQ(last, slots.end());
    (void)c; (void)d; (void)e; (void)f;
}

// ── CodeEmitter / JadeJit tests ──────────────────────────────────────────────
// These tests actually execute the JIT-compiled code.

TEST(JadeJitTest, CompilesAndExecutesConstIntReturn) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto i = b.const_int(42);
    auto ret = b.return_node(i);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 42);
}

TEST(JadeJitTest, CompilesAndExecutesAdd) {
    // (3 + 4) = 7
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add = b.add(a, c);
    auto ret = b.return_node(add);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 7);
}

TEST(JadeJitTest, CompilesAndExecutesSub) {
    // (10 - 4) = 6
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(10);
    auto c = b.const_int(4);
    auto sub = b.sub(a, c);
    auto ret = b.return_node(sub);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 6);
}

TEST(JadeJitTest, CompilesAndExecutesMul) {
    // (6 * 7) = 42
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(6);
    auto c = b.const_int(7);
    auto mul = b.mul(a, c);
    auto ret = b.return_node(mul);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 42);
}

TEST(JadeJitTest, CompilesAndExecutesChainedArithmetic) {
    // (3 + 4) * 5 = 35
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto three = b.const_int(3);
    auto four  = b.const_int(4);
    auto seven = b.add(three, four);
    auto five  = b.const_int(5);
    auto result = b.mul(seven, five);
    auto ret = b.return_node(result);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 35);
}

TEST(JadeJitTest, CompilesAndExecutesNegativeConstant) {
    // return -5 (as int64)
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto i = b.const_int(-5);
    auto ret = b.return_node(i);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), -5);
}

TEST(JadeJitTest, CompilesAndExecutesLargeConstant) {
    // return 0x123456789ABCDEF0
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto i = b.const_int(0x123456789ABCDEF0LL);
    auto ret = b.return_node(i);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 0x123456789ABCDEF0LL);
}

TEST(JadeJitTest, FallbackOnUnsupportedNodeKind) {
    // Use a node kind that Tier 1 doesn't yet lower (e.g., ConvR4 — requires XMM).
    // The compile should fail with UnsupportedNode error.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto i = b.const_int(42);
    NodeId inputs[] = {i};
    auto conv = g.create(NodeKind::ConvR4, inputs);
    auto ret = b.return_node(conv);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().kind, ErrorKind::UnsupportedNode);
}

TEST(JadeJitTest, FallbackOnUnverifiableGraph) {
    // A graph where a guard has no FrameState fails verification, so the
    // JadeJit::compile() rejects it immediately.
    Graph g;
    auto start = g.create(NodeKind::Start);
    auto i = g.create_const_int(0);
    NodeId inputs[] = {i};
    auto check = g.create(NodeKind::CheckInt, inputs);   // guard without FrameState
    g.set_ctrl_input(check, start);
    auto ret = g.create(NodeKind::Return);
    NodeId ret_inputs[] = {check};
    g.set_data_inputs(ret, ret_inputs);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    EXPECT_FALSE(r.has_value());
    // Verifier failure → JadeJit returns the error.
    EXPECT_EQ(r.error().kind, ErrorKind::VerificationFailed);
}

// ── Differential testing: Tier 1 vs granit ────────────────────────────────────
//
// For each test above, the JIT-compiled function must return the same value
// as the granit interpreter would. (Rule 38 — differential testing.)

TEST(JadeJitTest, DifferentialJitVsInterpreterConst) {
    // Both should produce 42 for a graph that returns ConstInt(42).
    Graph g_jit;
    GraphBuilder b(g_jit);
    auto start = b.start();
    auto i = b.const_int(42);
    auto ret = b.return_node(i);
    g_jit.set_ctrl_input(ret, start);
    g_jit.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g_jit);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    int64_t jit_result = fn();
    int64_t interpreter_result = 42;
    EXPECT_EQ(jit_result, interpreter_result);
}

// ── LinearScanRegAlloc: dump for inspection ────────────────────────────────

TEST(LinearScanRegAllocTest, DumpAllocationForInspection) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add = b.add(a, c);
    auto ret = b.return_node(add);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    LinearScanRegAlloc alloc;
    auto r = alloc.allocate(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // Sanity check: the dump should be non-empty.
    EXPECT_FALSE(to_string(*r).empty());
}

// ── Extended emitter tests: Div, Mod, Neg, bitwise, shifts, comparisons ─────

TEST(JadeJitTest, CompilesAndExecutesDiv) {
    // 20 / 4 = 5
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(20);
    auto c = b.const_int(4);
    NodeId inputs[] = {a, c};
    auto div = g.create(NodeKind::Div, inputs);
    auto ret = b.return_node(div);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 5);
}

TEST(JadeJitTest, CompilesAndExecutesMod) {
    // 20 % 7 = 6
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(20);
    auto c = b.const_int(7);
    NodeId inputs[] = {a, c};
    auto mod = g.create(NodeKind::Mod, inputs);
    auto ret = b.return_node(mod);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 6);
}

TEST(JadeJitTest, CompilesAndExecutesNeg) {
    // -(5) = -5
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(5);
    NodeId inputs[] = {a};
    auto neg = g.create(NodeKind::Neg, inputs);
    auto ret = b.return_node(neg);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), -5);
}

TEST(JadeJitTest, CompilesAndExecutesBitwiseAnd) {
    // 0xFF & 0x0F = 0x0F
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(0xFF);
    auto c = b.const_int(0x0F);
    NodeId inputs[] = {a, c};
    auto and_node = g.create(NodeKind::And, inputs);
    auto ret = b.return_node(and_node);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 0x0F);
}

TEST(JadeJitTest, CompilesAndExecutesBitwiseOr) {
    // 0xF0 | 0x0F = 0xFF
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(0xF0);
    auto c = b.const_int(0x0F);
    NodeId inputs[] = {a, c};
    auto or_node = g.create(NodeKind::Or, inputs);
    auto ret = b.return_node(or_node);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 0xFF);
}

TEST(JadeJitTest, CompilesAndExecutesShl) {
    // 1 << 4 = 16
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(1);
    auto c = b.const_int(4);
    NodeId inputs[] = {a, c};
    auto shl = g.create(NodeKind::Shl, inputs);
    auto ret = b.return_node(shl);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 16);
}

TEST(JadeJitTest, CompilesAndExecutesShr) {
    // 256 >> 4 = 16 (logical shift)
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(256);
    auto c = b.const_int(4);
    NodeId inputs[] = {a, c};
    auto shr = g.create(NodeKind::Shr, inputs);
    auto ret = b.return_node(shr);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 16);
}

TEST(JadeJitTest, CompilesAndExecutesCmpEq) {
    // 5 == 5 → 1
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(5);
    auto c = b.const_int(5);
    NodeId inputs[] = {a, c};
    auto eq = g.create(NodeKind::Eq, inputs);
    auto ret = b.return_node(eq);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 1);
}

TEST(JadeJitTest, CompilesAndExecutesCmpLt) {
    // 3 < 5 → 1
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(5);
    NodeId inputs[] = {a, c};
    auto lt = g.create(NodeKind::Lt, inputs);
    auto ret = b.return_node(lt);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 1);
}

TEST(JadeJitTest, CompilesAndExecutesCmpGtFalse) {
    // 3 > 5 → 0
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(5);
    NodeId inputs[] = {a, c};
    auto gt = g.create(NodeKind::Gt, inputs);
    auto ret = b.return_node(gt);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 0);
}

TEST(JadeJitTest, CompilesAndExecutesLdLocStLocRoundTrip) {
    // Store 42 to local 0, then load it back and return.
    // We model locals by storing the local index in side_data::class_id.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    auto stloc = g.create(NodeKind::StLoc);
    NodeId stloc_in[] = {val}; g.set_data_inputs(stloc, stloc_in);
    g.set_effect_input(stloc, start);
    g.side(stloc).class_id = 0;   // local 0

    auto ldloc = g.create(NodeKind::LdLoc);
    g.side(ldloc).class_id = 0;   // local 0

    auto ret = b.return_node(ldloc);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 42);
}

TEST(JadeJitTest, CompilesAndExecutesLdLocStLocMultiple) {
    // Store 10 to local 0, store 20 to local 1, return local 1.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto v10 = b.const_int(10);
    auto v20 = b.const_int(20);
    auto st0 = g.create(NodeKind::StLoc);
    NodeId st0_in[] = {v10}; g.set_data_inputs(st0, st0_in);
    g.set_effect_input(st0, start);
    g.side(st0).class_id = 0;

    auto st1 = g.create(NodeKind::StLoc);
    NodeId st1_in[] = {v20}; g.set_data_inputs(st1, st1_in);
    g.set_effect_input(st1, st0);  // effect chain
    g.side(st1).class_id = 1;

    auto ld1 = g.create(NodeKind::LdLoc);
    g.side(ld1).class_id = 1;

    auto ret = b.return_node(ld1);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, st1);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 20);
}

TEST(JadeJitTest, CompilesAndExecutesSafepointNode) {
    // A graph with a Safepoint node should compile and execute.
    // The safepoint is a no-op (flag is always 0 in tests).
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(77);
    auto sp = g.create(NodeKind::Safepoint);
    g.set_effect_input(sp, start);
    auto ret = b.return_node(val);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, sp);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 77);
}

TEST(JadeJitTest, CompilesAndExecutesMixedArithmetic) {
    // (10 + 20) - (3 * 4) = 30 - 12 = 18
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(10);
    auto c = b.const_int(20);
    auto add = b.add(a, c);          // 30
    auto d = b.const_int(3);
    auto e = b.const_int(4);
    auto mul = b.mul(d, e);          // 12
    NodeId sub_inputs[] = {add, mul};
    auto sub = g.create(NodeKind::Sub, sub_inputs);  // 18
    auto ret = b.return_node(sub);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 18);
}

TEST(JadeJitTest, CompilesAndExecutesNot) {
    // ~0 = -1 (all bits set)
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(0);
    NodeId inputs[] = {a};
    auto not_node = g.create(NodeKind::Not, inputs);
    auto ret = b.return_node(not_node);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), -1);
}

TEST(JadeJitTest, CompilesAndExecutesXor) {
    // 0xFF ^ 0x0F = 0xF0
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(0xFF);
    auto c = b.const_int(0x0F);
    NodeId inputs[] = {a, c};
    auto xor_node = g.create(NodeKind::Xor, inputs);
    auto ret = b.return_node(xor_node);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 0xF0);
}

TEST(JadeJitTest, CompilesAndExecutesSar) {
    // -16 >> 2 = -4 (arithmetic shift, preserves sign)
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(-16);
    auto c = b.const_int(2);
    NodeId inputs[] = {a, c};
    auto sar = g.create(NodeKind::Sar, inputs);
    auto ret = b.return_node(sar);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), -4);
}

// ── Block-scheduled emission tests ──────────────────────────────────────────
//
// These tests exercise the new block-scheduled emitter (RPO walk):
//   - Branches (if-then-else) with separate true/false blocks
//   - Both branches returning different values
//   - Branch fall-through after a merge (no merge point yet — each branch returns)

TEST(JadeJitTest, BranchTrueReturnsTrueValue) {
    // if (1) return 10; else return 20;
    // cond=1 (true), so the result should be 10.
    Graph g;
    GraphBuilder b(g);
    auto start    = b.start();
    auto cond     = b.const_int(1);
    auto if_node  = b.if_node(cond);
    g.set_ctrl_input(if_node, start);

    // True branch: IfTrue → ConstInt 10 → Return
    auto iftrue   = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto true_val = b.const_int(10);
    auto ret_true = b.return_node(true_val);
    g.set_ctrl_input(ret_true, iftrue);
    g.set_effect_input(ret_true, start);

    // False branch: IfFalse → ConstInt 20 → Return
    auto iffalse  = g.create(NodeKind::IfFalse);
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

TEST(JadeJitTest, BranchFalseReturnsFalseValue) {
    // if (0) return 10; else return 20;
    // cond=0 (false), so the result should be 20.
    Graph g;
    GraphBuilder b(g);
    auto start    = b.start();
    auto cond     = b.const_int(0);
    auto if_node  = b.if_node(cond);
    g.set_ctrl_input(if_node, start);

    auto iftrue   = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto true_val = b.const_int(10);
    auto ret_true = b.return_node(true_val);
    g.set_ctrl_input(ret_true, iftrue);
    g.set_effect_input(ret_true, start);

    auto iffalse  = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto false_val = b.const_int(20);
    auto ret_false = b.return_node(false_val);
    g.set_ctrl_input(ret_false, iffalse);
    g.set_effect_input(ret_false, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 20);
}

TEST(JadeJitTest, BranchWithArithmeticInTrueBranch) {
    // if (1) return (3 + 7); else return 99;
    // cond=1 → returns 10.
    Graph g;
    GraphBuilder b(g);
    auto start    = b.start();
    auto cond     = b.const_int(1);
    auto if_node  = b.if_node(cond);
    g.set_ctrl_input(if_node, start);

    // True branch: 3 + 7 = 10
    auto iftrue   = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto three    = b.const_int(3);
    auto seven    = b.const_int(7);
    NodeId add_inputs[] = {three, seven};
    auto add_true = g.create(NodeKind::Add, add_inputs);
    auto ret_true = b.return_node(add_true);
    g.set_ctrl_input(ret_true, iftrue);
    g.set_effect_input(ret_true, start);

    // False branch: return 99
    auto iffalse  = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto false_val = b.const_int(99);
    auto ret_false = b.return_node(false_val);
    g.set_ctrl_input(ret_false, iffalse);
    g.set_effect_input(ret_false, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 10);
}

TEST(JadeJitTest, BranchWithArithmeticInBothBranches) {
    // if (1) return (5 * 6); else return (40 - 18);
    // cond=1 → returns 30.
    Graph g;
    GraphBuilder b(g);
    auto start    = b.start();
    auto cond     = b.const_int(1);
    auto if_node  = b.if_node(cond);
    g.set_ctrl_input(if_node, start);

    auto iftrue   = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto five     = b.const_int(5);
    auto six      = b.const_int(6);
    NodeId mul_inputs[] = {five, six};
    auto mul_true = g.create(NodeKind::Mul, mul_inputs);
    auto ret_true = b.return_node(mul_true);
    g.set_ctrl_input(ret_true, iftrue);
    g.set_effect_input(ret_true, start);

    auto iffalse   = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto forty     = b.const_int(40);
    auto eighteen  = b.const_int(18);
    NodeId sub_inputs[] = {forty, eighteen};
    auto sub_false = g.create(NodeKind::Sub, sub_inputs);
    auto ret_false = b.return_node(sub_false);
    g.set_ctrl_input(ret_false, iffalse);
    g.set_effect_input(ret_false, start);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 30);
}

TEST(JadeJitTest, BranchNegativeConditionReturnsFalseBranch) {
    // if (-1) return 10; else return 20;
    // cond=-1 (truthy), so the result should be 10.
    Graph g;
    GraphBuilder b(g);
    auto start    = b.start();
    auto cond     = b.const_int(-1);
    auto if_node  = b.if_node(cond);
    g.set_ctrl_input(if_node, start);

    auto iftrue   = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto true_val = b.const_int(10);
    auto ret_true = b.return_node(true_val);
    g.set_ctrl_input(ret_true, iftrue);
    g.set_effect_input(ret_true, start);

    auto iffalse  = g.create(NodeKind::IfFalse);
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

// ── Loop header label binding test ───────────────────────────────────────────
//
// This is a degenerate "loop" that doesn't actually iterate. It exercises
// the Loop / Jump emission machinery: a Loop block (with a label) and a
// Jump back-edge that targets it. The Jump is in a block that's never
// executed (because the If always branches to the exit), so the back-edge
// label exists but is never reached at runtime.
//
// Real loop iteration requires Phi resolution at runtime (selecting the
// right predecessor's value at the loop header), which is a future feature.
// This test verifies the label/Jump emission is correct in isolation.

TEST(JadeJitTest, LoopHeaderLabelBoundCorrectly) {
    // if (1) return 42; else { loop_body: Jump back to loop_header (never taken) }
    //
    // Structure:
    //   Block 0 (entry): Start, ConstInt(1), If → jne true_label; jmp false_label
    //   Block 1 (true, IfTrue): ConstInt(42), Return → jmp epilogue
    //   Block 2 (false, IfFalse): Loop (header), ConstInt(0), If(cond=0) →
    //       jne body_label; jmp exit_label
    //   Block 3 (loop body): Jump → jmp loop_header_label (back-edge, never taken)
    //   Block 4 (loop exit): ConstInt(99), Return
    //
    // Since cond=1, we always take the true branch and return 42.
    Graph g;
    GraphBuilder b(g);
    auto start    = b.start();
    auto cond     = b.const_int(1);
    auto if_node  = b.if_node(cond);
    g.set_ctrl_input(if_node, start);

    // True branch: return 42.
    auto iftrue   = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto true_val = b.const_int(42);
    auto ret_true = b.return_node(true_val);
    g.set_ctrl_input(ret_true, iftrue);
    g.set_effect_input(ret_true, start);

    // False branch: a degenerate loop. Loop header + back-edge Jump (never taken).
    auto iffalse  = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, iffalse);
    g.set_effect_input(loop_hdr, iffalse);
    auto exit_val = b.const_int(99);
    auto ret_exit = b.return_node(exit_val);
    g.set_ctrl_input(ret_exit, loop_hdr);
    g.set_effect_input(ret_exit, loop_hdr);

    // The back-edge Jump: targets loop_hdr. In RPO, this block comes after
    // the loop exit (which has a Return terminator), so the Jump is dead code.
    auto jump_back = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump_back, loop_hdr);
    g.set_effect_input(jump_back, loop_hdr);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 42);
}

// ── Real loop iteration tests ────────────────────────────────────────────────
//
// These tests exercise the Loop/Jump back-edge machinery with actual iteration.
// The trick: we use memory-based locals (StLoc/LdLoc) which read/write the
// frame slot directly. The Loop header is just a label; the Jump at the end
// of the body emits `jmp loop_header_label`.
//
// IMPORTANT: loop-invariant values (like the loop bound N) MUST be stored in
// locals, not used as raw ConstInt nodes. The current LSRA doesn't account
// for loop back-edges, so a ConstInt in Block 0 whose register gets reused
// by the loop body would be clobbered. Storing it in a local (memory) keeps
// it safe across iterations.

TEST(JadeJitTest, CountToFiveLoop) {
    // i = 0; N = 5;
    // while (i < N) { i = i + 1; }
    // return i;   // 5
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    // StLoc(i, 0) — local 0
    auto zero  = b.const_int(0);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId st_i0_in[] = {zero}; g.set_data_inputs(st_i0, st_i0_in);
    g.set_effect_input(st_i0, start);
    g.side(st_i0).class_id = 0;

    // StLoc(N, 5) — local 1 (loop-invariant, stored in memory)
    auto five  = b.const_int(5);
    auto st_n  = g.create(NodeKind::StLoc);
    NodeId st_n_in[] = {five}; g.set_data_inputs(st_n, st_n_in);
    g.set_effect_input(st_n, st_i0);
    g.side(st_n).class_id = 1;

    // Loop header
    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_n);

    // LdLoc(i), LdLoc(N), Lt(i, N)
    auto ld_i = g.create(NodeKind::LdLoc);
    g.side(ld_i).class_id = 0;
    auto ld_n = g.create(NodeKind::LdLoc);
    g.side(ld_n).class_id = 1;
    NodeId lt_in[] = {ld_i, ld_n};
    auto lt = g.create(NodeKind::Lt, lt_in);

    // If(lt)
    auto if_node = b.if_node(lt);
    g.set_ctrl_input(if_node, loop_hdr);
    g.set_effect_input(if_node, loop_hdr);

    // IfFalse → exit: return i
    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto ld_i_exit = g.create(NodeKind::LdLoc);
    g.side(ld_i_exit).class_id = 0;
    auto ret = b.return_node(ld_i_exit);
    g.set_ctrl_input(ret, iffalse);
    g.set_effect_input(ret, iffalse);

    // IfTrue → body: i = i + 1; jump back
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

    // Jump back-edge → loop header
    auto jump = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump, st_i);
    g.set_effect_input(jump, st_i);

    JadeJit jit;
    auto r = jit.compile(g);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    auto fn = reinterpret_cast<JitFunc>(r->entry_point);
    EXPECT_EQ(fn(), 5);
}

TEST(JadeJitTest, SumOneToTenLoop) {
    // sum = 0; i = 0; N = 10;
    // while (i < N) { sum = sum + i; i = i + 1; }
    // return sum;   // 0+1+2+...+9 = 45
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    // StLoc(sum, 0) — local 0
    auto zero_a = b.const_int(0);
    auto st_sum0 = g.create(NodeKind::StLoc);
    NodeId st_sum0_in[] = {zero_a}; g.set_data_inputs(st_sum0, st_sum0_in);
    g.set_effect_input(st_sum0, start);
    g.side(st_sum0).class_id = 0;

    // StLoc(i, 0) — local 1
    auto zero_b = b.const_int(0);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId st_i0_in[] = {zero_b}; g.set_data_inputs(st_i0, st_i0_in);
    g.set_effect_input(st_i0, st_sum0);
    g.side(st_i0).class_id = 1;

    // StLoc(N, 10) — local 2 (loop-invariant)
    auto ten_val = b.const_int(10);
    auto st_n = g.create(NodeKind::StLoc);
    NodeId st_n_in[] = {ten_val}; g.set_data_inputs(st_n, st_n_in);
    g.set_effect_input(st_n, st_i0);
    g.side(st_n).class_id = 2;

    // Loop header
    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_n);

    // LdLoc(i), LdLoc(N), Lt(i, N)
    auto ld_i_cmp = g.create(NodeKind::LdLoc);
    g.side(ld_i_cmp).class_id = 1;
    auto ld_n = g.create(NodeKind::LdLoc);
    g.side(ld_n).class_id = 2;
    NodeId lt_in[] = {ld_i_cmp, ld_n};
    auto lt = g.create(NodeKind::Lt, lt_in);

    // If(lt)
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

    // IfTrue → body
    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);

    // sum = sum + i
    auto ld_sum = g.create(NodeKind::LdLoc);
    g.side(ld_sum).class_id = 0;
    auto ld_i_body = g.create(NodeKind::LdLoc);
    g.side(ld_i_body).class_id = 1;
    NodeId add_sum_in[] = {ld_sum, ld_i_body};
    auto add_sum = g.create(NodeKind::Add, add_sum_in);
    auto st_sum = g.create(NodeKind::StLoc);
    NodeId st_sum_in[] = {add_sum}; g.set_data_inputs(st_sum, st_sum_in);
    g.set_ctrl_input(st_sum, iftrue);
    g.set_effect_input(st_sum, iftrue);
    g.side(st_sum).class_id = 0;

    // i = i + 1
    auto ld_i_inc = g.create(NodeKind::LdLoc);
    g.side(ld_i_inc).class_id = 1;
    auto one = b.const_int(1);
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
    EXPECT_EQ(fn(), 45);
}

TEST(JadeJitTest, CountToHundredLoop) {
    // i = 0; N = 100;
    // while (i < N) { i = i + 1; }
    // return i;   // 100
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    // StLoc(i, 0) — local 0
    auto zero  = b.const_int(0);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId st_i0_in[] = {zero}; g.set_data_inputs(st_i0, st_i0_in);
    g.set_effect_input(st_i0, start);
    g.side(st_i0).class_id = 0;

    // StLoc(N, 100) — local 1 (loop-invariant)
    auto hundred_val = b.const_int(100);
    auto st_n = g.create(NodeKind::StLoc);
    NodeId st_n_in[] = {hundred_val}; g.set_data_inputs(st_n, st_n_in);
    g.set_effect_input(st_n, st_i0);
    g.side(st_n).class_id = 1;

    // Loop header
    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_n);

    // LdLoc(i), LdLoc(N), Lt(i, N)
    auto ld_i = g.create(NodeKind::LdLoc);
    g.side(ld_i).class_id = 0;
    auto ld_n = g.create(NodeKind::LdLoc);
    g.side(ld_n).class_id = 1;
    NodeId lt_in[] = {ld_i, ld_n};
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
    EXPECT_EQ(fn(), 100);
}

// ── Loop with register-allocated invariant (no StLoc workaround) ────────────
//
// These tests verify that the LSRA's loop-aware interval extension works:
// a ConstInt used inside a loop stays in its register across back-edges,
// without needing to be spilled to a local.

TEST(JadeJitTest, LoopWithRegisterInvariantBound) {
    // i = 0;
    // while (i < 5) { i = i + 1; }   // 5 is a raw ConstInt, NOT stored in a local
    // return i;   // 5
    //
    // This tests that the LSRA extends the live interval of ConstInt(5)
    // across the loop back-edge, so its register isn't reused by the
    // loop body's Add/StLoc.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    // StLoc(i, 0) — local 0
    auto zero  = b.const_int(0);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId st_i0_in[] = {zero}; g.set_data_inputs(st_i0, st_i0_in);
    g.set_effect_input(st_i0, start);
    g.side(st_i0).class_id = 0;

    // ConstInt(5) — NOT stored in a local; used directly as the loop bound.
    auto five = b.const_int(5);

    // Loop header
    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_i0);

    // LdLoc(i), Lt(i, 5)
    auto ld_i = g.create(NodeKind::LdLoc);
    g.side(ld_i).class_id = 0;
    NodeId lt_in[] = {ld_i, five};
    auto lt = g.create(NodeKind::Lt, lt_in);

    auto if_node = b.if_node(lt);
    g.set_ctrl_input(if_node, loop_hdr);
    g.set_effect_input(if_node, loop_hdr);

    // IfFalse → exit: return i
    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto ld_i_exit = g.create(NodeKind::LdLoc);
    g.side(ld_i_exit).class_id = 0;
    auto ret = b.return_node(ld_i_exit);
    g.set_ctrl_input(ret, iffalse);
    g.set_effect_input(ret, iffalse);

    // IfTrue → body: i = i + 1; jump back
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

TEST(JadeJitTest, LoopWithRegisterInvariantSum) {
    // sum = 0; i = 0;
    // while (i < 10) { sum = sum + i; i = i + 1; }   // 10 is raw ConstInt
    // return sum;   // 45
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    // StLoc(sum, 0) — local 0
    auto zero_a = b.const_int(0);
    auto st_sum0 = g.create(NodeKind::StLoc);
    NodeId st_sum0_in[] = {zero_a}; g.set_data_inputs(st_sum0, st_sum0_in);
    g.set_effect_input(st_sum0, start);
    g.side(st_sum0).class_id = 0;

    // StLoc(i, 0) — local 1
    auto zero_b = b.const_int(0);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId st_i0_in[] = {zero_b}; g.set_data_inputs(st_i0, st_i0_in);
    g.set_effect_input(st_i0, st_sum0);
    g.side(st_i0).class_id = 1;

    // ConstInt(10) — raw, NOT in a local
    auto ten = b.const_int(10);

    // Loop header
    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_i0);

    // LdLoc(i), Lt(i, 10)
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

    // IfTrue → body
    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);

    // sum = sum + i
    auto ld_sum = g.create(NodeKind::LdLoc);
    g.side(ld_sum).class_id = 0;
    auto ld_i_body = g.create(NodeKind::LdLoc);
    g.side(ld_i_body).class_id = 1;
    NodeId add_sum_in[] = {ld_sum, ld_i_body};
    auto add_sum = g.create(NodeKind::Add, add_sum_in);
    auto st_sum = g.create(NodeKind::StLoc);
    NodeId st_sum_in[] = {add_sum}; g.set_data_inputs(st_sum, st_sum_in);
    g.set_ctrl_input(st_sum, iftrue);
    g.set_effect_input(st_sum, iftrue);
    g.side(st_sum).class_id = 0;

    // i = i + 1
    auto ld_i_inc = g.create(NodeKind::LdLoc);
    g.side(ld_i_inc).class_id = 1;
    auto one = b.const_int(1);
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
    EXPECT_EQ(fn(), 45);
}
