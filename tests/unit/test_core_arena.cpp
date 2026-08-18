// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_core_arena.cpp

#include <gtest/gtest.h>
#include "jade/core/Arena.hpp"
#include "jade/core/NodeId.hpp"

using namespace jade;

TEST(BumpAllocatorTest, AllocateReturnsAlignedMemory) {
    BumpAllocator a;
    void* p = a.allocate(16, alignof(std::max_align_t));
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % alignof(std::max_align_t), 0u);
}

TEST(BumpAllocatorTest, MultipleAllocationsAreSequential) {
    BumpAllocator a;
    void* p1 = a.allocate(32, 8);
    void* p2 = a.allocate(32, 8);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_NE(p1, p2);
    // p2 should be at least 32 bytes past p1
    EXPECT_GE(static_cast<std::byte*>(p2) - static_cast<std::byte*>(p1), 32);
}

TEST(BumpAllocatorTest, ConstructReturnsInitializedObject) {
    BumpAllocator a;
    struct Foo { int x; float y; };
    Foo* f = a.construct<Foo>(42, 3.14f);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->x, 42);
    EXPECT_FLOAT_EQ(f->y, 3.14f);
}

TEST(BumpAllocatorTest, ConstructArrayReturnsSpan) {
    BumpAllocator a;
    auto span = a.construct_array<int>(100);
    EXPECT_EQ(span.size(), 100u);
    for (int x : span) EXPECT_EQ(x, 0);
}

TEST(BumpAllocatorTest, ResetAllowsReuseWithoutGrowing) {
    BumpAllocator a;
    (void)a.allocate(1024, 8);
    std::size_t cap_before = a.total_capacity();
    a.reset();
    (void)a.allocate(1024, 8);
    EXPECT_EQ(a.total_capacity(), cap_before);
}

TEST(BumpAllocatorTest, BytesUsedGrowsWithAllocations) {
    BumpAllocator a;
    EXPECT_EQ(a.bytes_used(), 0u);
    (void)a.allocate(100, 1);
    EXPECT_GE(a.bytes_used(), 100u);
}

TEST(BumpAllocatorTest, LargeAllocationTriggersChunkGrowth) {
    BumpAllocator a{4096};  // small initial chunk
    void* p = a.allocate(8192, 8);  // bigger than chunk
    ASSERT_NE(p, nullptr);
    EXPECT_GE(a.total_capacity(), 8192u);
}

TEST(EdgePoolTest, AllocReturnsContiguousSlice) {
    EdgePool pool;
    auto [first, span] = pool.alloc(3);
    EXPECT_EQ(span.size(), 3u);
    span[0] = NodeId{1};
    span[1] = NodeId{2};
    span[2] = NodeId{3};
    auto view = pool.get(first, 3);
    EXPECT_EQ(view[0], NodeId{1});
    EXPECT_EQ(view[1], NodeId{2});
    EXPECT_EQ(view[2], NodeId{3});
}

TEST(EdgePoolTest, MultipleAllocsAreContiguous) {
    EdgePool pool;
    auto [f1, s1] = pool.alloc(2);
    auto [f2, s2] = pool.alloc(2);
    EXPECT_EQ(f2, f1 + 2);
}
