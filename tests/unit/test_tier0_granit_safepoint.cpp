// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_tier0_granit_safepoint.cpp

#include <gtest/gtest.h>
#include "jade/tier0_granit/Interpreter.hpp"
#include "jade/tier0_granit/Bytecode.hpp"
#include "jade/runtime/Safepoint.hpp"

#include <thread>
#include <atomic>
#include <chrono>

using namespace jade;
using namespace jade::granit;

TEST(SafepointTest, ThreadStatePollFlagDefaultsToFalse) {
    SafepointManager mgr;
    auto* ts = mgr.register_thread();
    EXPECT_FALSE(ts->poll_requested.load());
    EXPECT_FALSE(ts->at_safepoint.load());
}

TEST(SafepointTest, RequestGlobalSafepointSetsAllFlags) {
    SafepointManager mgr;
    auto* ts1 = mgr.register_thread();
    auto* ts2 = mgr.register_thread();
    // Mark both as at safepoint to simulate quick response
    ts1->at_safepoint.store(true);
    ts2->at_safepoint.store(true);
    EXPECT_TRUE(mgr.request_global_safepoint(100));
    EXPECT_TRUE(ts1->poll_requested.load());
    EXPECT_TRUE(ts2->poll_requested.load());
}

TEST(SafepointTest, ShouldPollReturnsTrueWhenRequested) {
    SafepointManager mgr;
    auto* ts = mgr.register_thread();
    EXPECT_FALSE(SafepointManager::should_poll(ts));
    ts->poll_requested.store(true);
    EXPECT_TRUE(SafepointManager::should_poll(ts));
}

TEST(SafepointTest, InterpreterReachesSafepointOnBackEdge) {
    // Build a tiny loop: increment a counter up to N, then return.
    ProgramBuilder b;
    b.push_const_i(0);          // 0: push 0 (counter)
    // loop start at pc=1
    b.dup();                    // 1: dup top
    b.push_const_i(1);          // 2: push 1
    b.add();                    // 3: add
    b.dup();                    // 4: dup top
    b.push_const_i(1'000'000); // 5: push 1M (so the loop runs long enough to be safepoint-polled)
    b.lt();                     // 6: cmp
    b.jump_if_true(1);          // 7: jump back to 1 if not done
    b.ret();                    // 8: ret
    Program prog = b.build();

    SafepointManager mgr;
    auto* ts = mgr.register_thread();

    Interpreter interp;
    interp.set_safepoint_manager(&mgr, ts);

    std::atomic<bool> reached_safepoint{false};
    ts->at_safepoint.store(false);

    // Spin a watcher thread that requests a safepoint shortly after the
    // interpreter starts.
    std::thread watcher([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        reached_safepoint.store(mgr.request_global_safepoint(2000));
        mgr.release_safepoint();
    });

    auto r = interp.run(prog);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    watcher.join();
    EXPECT_TRUE(reached_safepoint.load()) << "interpreter did not reach safepoint in time";
}

TEST(SafepointTest, EnterSafepointBlocksUntilReleased) {
    SafepointManager mgr;
    auto* ts = mgr.register_thread();
    ts->poll_requested.store(true);

    std::atomic<bool> entered{false};
    std::atomic<bool> exited{false};

    std::thread t([&]() {
        SafepointManager::enter_safepoint(ts);
        entered.store(true);
        exited.store(true);
    });

    // Give the thread time to enter the safepoint.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(ts->at_safepoint.load());
    EXPECT_FALSE(entered.load());

    // Release — thread should exit.
    ts->poll_requested.store(false);
    t.join();
    EXPECT_TRUE(exited.load());
}
