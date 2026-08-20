// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_build_regions.cpp
//
// Tests for the BuildRegions pass: block identification, edge connection,
// loop detection, and dominator tree computation.

#include <gtest/gtest.h>
#include "jade/ir/Graph.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/passes/BuildRegions.hpp"

#include <algorithm>
#include <set>

using namespace jade;

TEST(BuildRegionsTest, EmptyGraph) {
    Graph g;
    auto bs = build_block_structure(g);
    EXPECT_EQ(bs.num_blocks(), 0u);
}

TEST(BuildRegionsTest, SingleBlockLinearGraph) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add = b.add(a, c);
    auto ret = b.return_node(add);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    auto bs = build_block_structure(g);
    // Should have at least 1 block (the linear sequence).
    EXPECT_GE(bs.num_blocks(), 1u);
    // The Start node should be in block 0.
    EXPECT_EQ(bs.block_of(start), 0u);
}

TEST(BuildRegionsTest, IfThenElseProducesMultipleBlocks) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto cond = b.const_int(1);
    auto if_node = b.if_node(cond);
    g.set_ctrl_input(if_node, start);
    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto false_val = b.const_int(10);
    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto true_val = b.const_int(20);
    auto ret = b.return_node(true_val);
    g.set_ctrl_input(ret, iftrue);

    auto bs = build_block_structure(g);
    // Should have multiple blocks: entry, true branch, false branch.
    EXPECT_GT(bs.num_blocks(), 1u);
    // IfTrue and IfFalse should be in different blocks.
    uint32_t true_block = bs.block_of(iftrue);
    uint32_t false_block = bs.block_of(iffalse);
    EXPECT_NE(true_block, false_block);
}

TEST(BuildRegionsTest, ReturnEndsBlock) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    auto ret = b.return_node(val);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    // Add another node after Return (dead code, but present in the graph).
    auto dead = b.const_int(99);

    auto bs = build_block_structure(g);
    // The Return should end one block; the dead node should start a new block.
    uint32_t ret_block = bs.block_of(ret);
    uint32_t dead_block = bs.block_of(dead);
    EXPECT_NE(ret_block, dead_block);
}

TEST(BuildRegionsTest, DominatorTree) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add = b.add(a, c);
    auto ret = b.return_node(add);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    auto bs = build_block_structure(g);
    // Block 0 should dominate all other blocks.
    for (uint32_t i = 1; i < bs.num_blocks(); ++i) {
        EXPECT_TRUE(bs.dominates(0, i));
    }
}

TEST(BuildRegionsTest, DominatesSelf) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    auto ret = b.return_node(val);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    auto bs = build_block_structure(g);
    for (uint32_t i = 0; i < bs.num_blocks(); ++i) {
        EXPECT_TRUE(bs.dominates(i, i));
    }
}

TEST(BuildRegionsTest, LoopDetection) {
    // Build a simple loop: Start → If → IfTrue (body) → back to If → IfFalse (exit)
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto cond = b.const_int(1);
    auto if_node = b.if_node(cond);
    g.set_ctrl_input(if_node, start);
    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);

    auto bs = build_block_structure(g);
    // The block structure should be computed without crashing.
    EXPECT_GT(bs.num_blocks(), 0u);
}

TEST(BuildRegionsTest, BlockOfNode) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(1);
    auto c = b.const_int(2);
    auto add = b.add(a, c);
    auto ret = b.return_node(add);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    auto bs = build_block_structure(g);
    // Every non-dead node should have a block assignment.
    EXPECT_EQ(bs.block_of(start), 0u);
    EXPECT_EQ(bs.block_of(a), 0u);
    EXPECT_EQ(bs.block_of(c), 0u);
    EXPECT_EQ(bs.block_of(add), 0u);
    // Return ends the block; it's in the same block.
    EXPECT_EQ(bs.block_of(ret), 0u);
}

TEST(BuildRegionsTest, MultipleReturns) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto cond = b.const_int(1);
    auto if_node = b.if_node(cond);
    g.set_ctrl_input(if_node, start);
    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto true_val = b.const_int(10);
    auto ret1 = b.return_node(true_val);
    g.set_ctrl_input(ret1, iftrue);
    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto false_val = b.const_int(20);
    auto ret2 = b.return_node(false_val);
    g.set_ctrl_input(ret2, iffalse);

    auto bs = build_block_structure(g);
    // Both returns should be in different blocks.
    uint32_t ret1_block = bs.block_of(ret1);
    uint32_t ret2_block = bs.block_of(ret2);
    EXPECT_NE(ret1_block, ret2_block);
    // ret1 and ret2 have no successors (terminal blocks).
    if (ret1_block < bs.num_blocks()) {
        EXPECT_EQ(bs.blocks[ret1_block].successors.size(), 0u);
    }
    if (ret2_block < bs.num_blocks()) {
        EXPECT_EQ(bs.blocks[ret2_block].successors.size(), 0u);
    }
}

// ── RPO traversal tests ──────────────────────────────────────────────────────

TEST(BuildRegionsTest, RPOOfLinearGraph) {
    // Linear graph: Start, ConstInt, Add, Return — should be 1 block.
    // RPO should be [0].
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add = b.add(a, c);
    auto ret = b.return_node(add);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    auto bs = build_block_structure(g);
    auto rpo = bs.reverse_post_order();
    EXPECT_FALSE(rpo.empty());
    // First block should be the entry (block 0).
    EXPECT_EQ(rpo.front(), 0u);
}

TEST(BuildRegionsTest, RPOOfIfThenElseVisitsAllReachableBlocks) {
    // if (1) return 10; else return 20;
    // Should have 3 blocks: entry (Start, cond, If), true (IfTrue, val, Return),
    // false (IfFalse, val, Return).
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto cond = b.const_int(1);
    auto if_node = b.if_node(cond);
    g.set_ctrl_input(if_node, start);
    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    auto true_val = b.const_int(10);
    auto ret1 = b.return_node(true_val);
    g.set_ctrl_input(ret1, iftrue);
    g.set_effect_input(ret1, start);
    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto false_val = b.const_int(20);
    auto ret2 = b.return_node(false_val);
    g.set_ctrl_input(ret2, iffalse);
    g.set_effect_input(ret2, start);

    auto bs = build_block_structure(g);
    auto rpo = bs.reverse_post_order();
    // All 3 reachable blocks should appear in RPO.
    // (Some dead/floating blocks may exist but they shouldn't be visited
    // because no control edge enters them.)
    EXPECT_GE(rpo.size(), 3u);
    // Entry block (0) must be first.
    EXPECT_EQ(rpo.front(), 0u);
    // No duplicates.
    std::set<uint32_t> seen(rpo.begin(), rpo.end());
    EXPECT_EQ(seen.size(), rpo.size());
}

TEST(BuildRegionsTest, NodeIdsInBlockReturnsContiguousRange) {
    // Linear graph: 5 nodes, all in block 0.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(1);
    auto c = b.const_int(2);
    auto add = b.add(a, c);
    auto ret = b.return_node(add);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    auto bs = build_block_structure(g);
    auto ids = bs.node_ids_in_block(g, 0);
    // Should include all 5 live nodes (in NodeId order).
    EXPECT_EQ(ids.size(), 5u);
    EXPECT_EQ(ids[0], start.value);
    EXPECT_EQ(ids[1], a.value);
    EXPECT_EQ(ids[2], c.value);
    EXPECT_EQ(ids[3], add.value);
    EXPECT_EQ(ids[4], ret.value);
}

TEST(BuildRegionsTest, NodeIdsInBlockSkipsDeadNodes) {
    // Build a graph with a dead node, ensure it's skipped.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(1);
    auto ret = b.return_node(a);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    auto dead = b.const_int(99);
    g.mark_dead(dead);

    auto bs = build_block_structure(g);
    auto ids = bs.node_ids_in_block(g, 0);
    // Should not contain the dead node.
    EXPECT_EQ(std::find(ids.begin(), ids.end(), dead.value), ids.end());
}
