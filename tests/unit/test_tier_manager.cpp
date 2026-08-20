// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_tier_manager.cpp
//
// Tests for the TierManager and TieredDispatch — the tier escalation
// dispatch loop that triggers compilation at the right invocation counts.

#include <gtest/gtest.h>
#include "jade/runtime/TierManager.hpp"
#include "jade/runtime/TieredDispatch.hpp"
#include "jade/tier0_granit/Value.hpp"

#include <vector>
#include <cstdint>

using namespace jade;
using namespace jade::granit;

namespace {

// Minimal JVM bytecode: iconst_5; ireturn → returns 5.
// This is the simplest method that exercises the interpreter.
[[nodiscard]] std::vector<uint8_t> make_simple_method() {
    return {0x08, 0xAC};   // iconst_5; ireturn
}

// JVM bytecode: iconst_0; istore_0; iinc 0, 1; iload_0; ireturn → returns 1.
[[nodiscard]] std::vector<uint8_t> make_inc_method() {
    return {0x03, 0x3B, 0x84, 0x00, 0x01, 0x1A, 0xAC};
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// TierManager unit tests (no JIT, just escalation logic)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TierManagerTest, StartsAtInterpreter) {
    TierManager mgr;
    MethodHandle m;
    m.name = "test";
    m.current_tier = Tier::Interpreter;

    // First invocation should stay at interpreter (count=1 < 100).
    Tier t = mgr.on_invocation(m);
    EXPECT_EQ(t, Tier::Interpreter);
    EXPECT_EQ(m.current_tier, Tier::Interpreter);
}

TEST(TierManagerTest, EscalatesToBaselineAt100) {
    TierManager mgr;
    MethodHandle m;
    m.name = "test";
    m.current_tier = Tier::Interpreter;

    // Invoke 99 times — should stay at interpreter.
    for (int i = 0; i < 99; ++i) {
        mgr.on_invocation(m);
    }
    EXPECT_EQ(m.current_tier, Tier::Interpreter);

    // 100th invocation — should escalate to Baseline.
    Tier t = mgr.on_invocation(m);
    EXPECT_EQ(t, Tier::Baseline);
}

TEST(TierManagerTest, EscalatesToOptimizingAt1000) {
    TierManager mgr;
    MethodHandle m;
    m.name = "test";
    m.current_tier = Tier::Baseline;

    // Invoke 999 times — should stay at baseline.
    for (int i = 0; i < 999; ++i) {
        mgr.on_invocation(m);
    }
    EXPECT_EQ(m.current_tier, Tier::Baseline);

    // 1000th invocation — should escalate to Optimizing.
    Tier t = mgr.on_invocation(m);
    EXPECT_EQ(t, Tier::Optimizing);
}

TEST(TierManagerTest, EscalatesToPeakAt10000) {
    TierManager mgr;
    MethodHandle m;
    m.name = "test";
    m.current_tier = Tier::Optimizing;

    // Invoke 9999 times — should stay at optimizing.
    for (int i = 0; i < 9999; ++i) {
        mgr.on_invocation(m);
    }
    EXPECT_EQ(m.current_tier, Tier::Optimizing);

    // 10000th invocation — should escalate to Peak.
    Tier t = mgr.on_invocation(m);
    EXPECT_EQ(t, Tier::Peak);
}

TEST(TierManagerTest, CustomThresholds) {
    TierThresholds t;
    t.interpreter_to_baseline = 5;
    t.baseline_to_optimizing = 10;
    t.optimizing_to_peak = 20;
    TierManager mgr{t};
    MethodHandle m;
    m.current_tier = Tier::Interpreter;

    for (int i = 0; i < 4; ++i) mgr.on_invocation(m);
    EXPECT_EQ(m.current_tier, Tier::Interpreter);

    Tier t1 = mgr.on_invocation(m);  // 5th
    EXPECT_EQ(t1, Tier::Baseline);

    // Mark as compiled at baseline.
    mgr.mark_compiled(m, Tier::Baseline, reinterpret_cast<void*>(0x1000));

    for (int i = 0; i < 4; ++i) mgr.on_invocation(m);  // 6..9
    EXPECT_EQ(m.current_tier, Tier::Baseline);

    Tier t2 = mgr.on_invocation(m);  // 10th
    EXPECT_EQ(t2, Tier::Optimizing);

    // Mark as compiled at optimizing.
    mgr.mark_compiled(m, Tier::Optimizing, reinterpret_cast<void*>(0x2000));

    for (int i = 0; i < 9; ++i) mgr.on_invocation(m);  // 11..19
    EXPECT_EQ(m.current_tier, Tier::Optimizing);

    Tier t3 = mgr.on_invocation(m);  // 20th
    EXPECT_EQ(t3, Tier::Peak);
}

TEST(TierManagerTest, MarkCompiledSetsEntry) {
    TierManager mgr;
    MethodHandle m;
    m.current_tier = Tier::Interpreter;

    mgr.mark_compiled(m, Tier::Baseline, reinterpret_cast<void*>(0x1000));
    EXPECT_EQ(m.current_tier, Tier::Baseline);
    EXPECT_EQ(m.baseline_entry, reinterpret_cast<void*>(0x1000));

    mgr.mark_compiled(m, Tier::Optimizing, reinterpret_cast<void*>(0x2000));
    EXPECT_EQ(m.current_tier, Tier::Optimizing);
    EXPECT_EQ(m.optimizing_entry, reinterpret_cast<void*>(0x2000));

    mgr.mark_compiled(m, Tier::Peak, reinterpret_cast<void*>(0x3000));
    EXPECT_EQ(m.current_tier, Tier::Peak);
    EXPECT_EQ(m.peak_entry, reinterpret_cast<void*>(0x3000));
}

TEST(TierManagerTest, EntryReturnsBestAvailable) {
    MethodHandle m;
    m.interpreter_entry = reinterpret_cast<void*>(0x100);
    m.baseline_entry = reinterpret_cast<void*>(0x200);
    m.optimizing_entry = reinterpret_cast<void*>(0x300);
    m.peak_entry = reinterpret_cast<void*>(0x400);

    // entry() returns the best (highest tier) available.
    EXPECT_EQ(m.entry(), reinterpret_cast<void*>(0x400));

    m.peak_entry = nullptr;
    EXPECT_EQ(m.entry(), reinterpret_cast<void*>(0x300));

    m.optimizing_entry = nullptr;
    EXPECT_EQ(m.entry(), reinterpret_cast<void*>(0x200));

    m.baseline_entry = nullptr;
    EXPECT_EQ(m.entry(), reinterpret_cast<void*>(0x100));
}

// ═══════════════════════════════════════════════════════════════════════════════
// TieredDispatch integration tests (with interpreter execution)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TieredDispatchTest, InterpreterOnlyWithLowThresholds) {
    // With thresholds set high enough, the method stays at interpreter.
    TieredDispatch dispatch;
    auto bytes = make_simple_method();

    // Invoke 3 times — should stay at interpreter.
    for (int i = 0; i < 3; ++i) {
        auto r = dispatch.invoke("test.simple", bytes, 1, 0);
        ASSERT_TRUE(r.has_value()) << r.error().what();
        EXPECT_EQ(r->as_int32(), 5);
    }
    EXPECT_EQ(dispatch.current_tier("test.simple"), Tier::Interpreter);
    EXPECT_EQ(dispatch.invocation_count("test.simple"), 3u);
}

TEST(TieredDispatchTest, EscalatesToBaselineWithLowThreshold) {
    // Set threshold to 3 — should escalate after 3 invocations.
    TieredDispatch dispatch;
    TierThresholds t;
    t.interpreter_to_baseline = 3;
    t.baseline_to_optimizing = 1000;
    t.optimizing_to_peak = 10000;
    dispatch.set_thresholds(t);

    auto bytes = make_simple_method();

    // 3 invocations — should escalate to baseline.
    for (int i = 0; i < 3; ++i) {
        auto r = dispatch.invoke("test.simple", bytes, 1, 0);
        ASSERT_TRUE(r.has_value()) << r.error().what();
        EXPECT_EQ(r->as_int32(), 5);
    }

    // The tier should have escalated to Baseline (JADE JIT compilation
    // triggered, but execution falls back to interpreter for now).
    EXPECT_EQ(dispatch.invocation_count("test.simple"), 3u);
    // After 3 invocations with threshold=3, the tier should be Baseline.
    // (The JIT compilation may fail if the method is too simple, but the
    // TierManager marks it as Baseline regardless.)
    EXPECT_EQ(dispatch.current_tier("test.simple"), Tier::Baseline);
}

TEST(TieredDispatchTest, CorrectResultsAcrossTiers) {
    // Verify the method returns the correct result at every tier.
    TieredDispatch dispatch;
    TierThresholds t;
    t.interpreter_to_baseline = 2;
    t.baseline_to_optimizing = 4;
    t.optimizing_to_peak = 6;
    dispatch.set_thresholds(t);

    auto bytes = make_inc_method();  // returns 1

    // 6 invocations — should escalate through all tiers.
    for (int i = 0; i < 6; ++i) {
        auto r = dispatch.invoke("test.inc", bytes, 2, 0);
        ASSERT_TRUE(r.has_value()) << r.error().what();
        EXPECT_EQ(r->as_int32(), 1)
            << "Result must be correct at invocation " << i;
    }

    EXPECT_EQ(dispatch.invocation_count("test.inc"), 6u);
}

TEST(TieredDispatchTest, MultipleMethodsTrackedSeparately) {
    TieredDispatch dispatch;
    dispatch.set_thresholds({3, 1000, 10000});

    auto bytes_a = make_simple_method();   // returns 5
    auto bytes_b = make_inc_method();       // returns 1

    // Invoke method A 3 times — should escalate.
    for (int i = 0; i < 3; ++i) {
        dispatch.invoke("A.simple", bytes_a, 1, 0);
    }

    // Invoke method B 1 time — should NOT escalate yet.
    dispatch.invoke("B.inc", bytes_b, 2, 0);

    EXPECT_EQ(dispatch.current_tier("A.simple"), Tier::Baseline);
    EXPECT_EQ(dispatch.current_tier("B.inc"), Tier::Interpreter);
    EXPECT_EQ(dispatch.invocation_count("A.simple"), 3u);
    EXPECT_EQ(dispatch.invocation_count("B.inc"), 1u);
}

TEST(TieredDispatchTest, InvocationCountAccurate) {
    TieredDispatch dispatch;
    auto bytes = make_simple_method();

    for (int i = 1; i <= 10; ++i) {
        dispatch.invoke("test.count", bytes, 1, 0);
        EXPECT_EQ(dispatch.invocation_count("test.count"), static_cast<uint32_t>(i));
    }
}
