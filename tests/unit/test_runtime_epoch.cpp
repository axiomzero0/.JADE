// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_runtime_epoch.cpp

#include <gtest/gtest.h>
#include "jade/runtime/Epoch.hpp"

#include <thread>
#include <atomic>

using namespace jade;

TEST(EpochTest, RegisterThreadReturnsSlot) {
    EpochGC gc;
    uint32_t s1 = gc.register_thread();
    uint32_t s2 = gc.register_thread();
    EXPECT_NE(s1, s2);
}

TEST(EpochTest, EnterSetsLocalEpoch) {
    EpochGC gc;
    uint32_t slot = gc.register_thread();
    Epoch e = gc.enter(slot);
    EXPECT_GT(e, 0u);
    gc.exit(slot);
}

TEST(EpochTest, RetireAddsToRetiredList) {
    EpochGC gc;
    uint32_t slot = gc.register_thread();
    Epoch e = gc.enter(slot);

    int* leaked = new int(42);
    gc.retire(e, leaked, [](void* p) noexcept { delete static_cast<int*>(p); });

    EXPECT_EQ(gc.retired_count(), 1u);
    gc.exit(slot);
}

TEST(EpochTest, ReclaimFreesRetiredAfterThreadsAdvance) {
    EpochGC gc;
    uint32_t slot = gc.register_thread();
    Epoch e = gc.enter(slot);

    std::atomic<bool> freed{false};
    auto* ptr = new std::atomic<bool>{false};
    gc.retire(e, ptr, [](void* p) noexcept {
        delete static_cast<std::atomic<bool>*>(p);
        // Can't write to a deleted bool; we use a separate flag.
        // The deleter just frees the memory.
        (void)p;
    });
    (void)freed;

    // Exit the epoch so the retired node can be reclaimed.
    gc.exit(slot);
    gc.try_reclaim();
    EXPECT_EQ(gc.retired_count(), 0u);
}

TEST(EpochTest, ScopedEpochRAII) {
    EpochGC gc;
    uint32_t slot = gc.register_thread();
    {
        ScopedEpoch se{gc, slot};
        EXPECT_GT(se.epoch(), 0u);
    }
    // After scope, the thread's epoch should be 0 (idle).
}

TEST(EpochTest, ReclaimDoesNotFreeWhenThreadIsActive) {
    EpochGC gc;
    uint32_t slot = gc.register_thread();
    Epoch e = gc.enter(slot);

    int* leaked = new int(42);
    gc.retire(e, leaked, [](void* p) noexcept { delete static_cast<int*>(p); });
    // Try to reclaim while thread is still active in epoch `e`.
    gc.try_reclaim();
    // The retired node should still be there because the thread is still in epoch e.
    EXPECT_EQ(gc.retired_count(), 1u);
    gc.exit(slot);
    gc.try_reclaim();
    EXPECT_EQ(gc.retired_count(), 0u);
}

TEST(EpochTest, MultipleThreadsCanRegisterAndEnter) {
    EpochGC gc;
    std::vector<std::thread> threads;
    std::atomic<int> active_count{0};

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            uint32_t slot = gc.register_thread();
            ScopedEpoch se{gc, slot};
            active_count.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(active_count.load(), 4);
}
