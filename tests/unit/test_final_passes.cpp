// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_final_passes.cpp
//
// Tests for the final batch of real passes: LoopUnrolling,
// ProfileGuidedBlockReorder, ICStubEmission. These were previously
// no-ops; now they perform real transformations.

#include <gtest/gtest.h>
#include "jade/ir/Graph.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"
#include "jade/tier3_diamond/LoopUnrolling.hpp"
#include "jade/ir/passes/ProfileGuidedBlockReorder.hpp"
#include "jade/ir/passes/ICStubEmission.hpp"
#include "jade/ir/passes/DeadCodeElimination.hpp"

using namespace jade;
using namespace jade::tier3;

namespace {

[[nodiscard]] int count_live(const Graph& g, NodeKind k) {
    int n = 0;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        if (!g.node(id).is_dead() && g.node(id).kind == k) ++n;
    }
    return n;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// LoopUnrolling tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(LoopUnrollingTest, RunsOnLoopGraph) {
    // Build a simple loop: i=0; while(i<5) i++; return i
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto zero = b.const_int(0);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId si[] = {zero}; g.set_data_inputs(st_i0, si);
    g.set_effect_input(st_i0, start);
    g.side(st_i0).class_id = 0;

    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_i0);

    auto ld_i = g.create(NodeKind::LdLoc);
    g.side(ld_i).class_id = 0;
    auto five = b.const_int(5);
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
    NodeId si2[] = {add}; g.set_data_inputs(st_i, si2);
    g.set_ctrl_input(st_i, iftrue);
    g.set_effect_input(st_i, iftrue);
    g.side(st_i).class_id = 0;
    auto jump = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump, st_i);
    g.set_effect_input(jump, st_i);

    LoopUnrollingPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    SUCCEED() << "LoopUnrolling ran successfully";
}

TEST(LoopUnrollingTest, DoesNotCrashOnEmptyGraph) {
    Graph g;
    LoopUnrollingPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
}

// ═══════════════════════════════════════════════════════════════════════════════
// ProfileGuidedBlockReorder tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ProfileGuidedBlockReorderTest, MarksThrowBlocksAsCold) {
    // A graph with a Throw node — the block containing Throw should be
    // marked IsCold.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    auto throw_node = g.create(NodeKind::Throw);
    NodeId th_in[] = {val};
    g.set_data_inputs(throw_node, th_in);
    g.set_ctrl_input(throw_node, start);
    g.set_effect_input(throw_node, start);

    ProfileGuidedBlockReorderPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();

    // The Throw node's block should be marked IsCold.
    // (The pass marks the block leader, which may be the Throw itself.)
    bool found_cold = false;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        if (g.node(id).is_dead()) continue;
        if (g.node(id).flags.has(NodeFlag::IsCold)) {
            found_cold = true;
            break;
        }
    }
    EXPECT_TRUE(found_cold) << "Throw block should be marked IsCold";
}

TEST(ProfileGuidedBlockReorderTest, DoesNotMarkHotBlocksAsCold) {
    // A simple arithmetic graph (no Throw/Deopt) — no blocks should be
    // marked IsCold.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto c = b.const_int(4);
    auto add = b.add(a, c);
    auto ret = b.return_node(add);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);

    ProfileGuidedBlockReorderPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();

    // No node should have IsCold.
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        if (g.node(id).is_dead()) continue;
        EXPECT_FALSE(g.node(id).flags.has(NodeFlag::IsCold))
            << "Node %" << id.value << " should NOT be IsCold";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ICStubEmission tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ICStubEmissionTest, InsertsCheckClassBeforeCallVirt) {
    // callvirt method(receiver) — should get a CheckClass guard.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto recv = b.const_int(0);  // receiver (placeholder)
    NodeId callvirt_in[] = {recv};
    auto callvirt = g.create(NodeKind::CallVirt, callvirt_in);
    g.set_effect_input(callvirt, start);
    auto ret = b.return_node(callvirt);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, callvirt);

    ICStubEmissionPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();

    // A CheckClass node should have been inserted.
    EXPECT_GE(count_live(g, NodeKind::CheckClass), 1)
        << "ICStubEmission should insert a CheckClass guard before CallVirt";
}

TEST(ICStubEmissionTest, DoesNotInsertCheckForCallKnown) {
    // CallKnown (already devirtualized) should NOT get a CheckClass.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto callee = b.const_int(0);
    auto arg = b.const_int(42);
    NodeId call_in[] = {callee, arg};
    auto call = g.create(NodeKind::CallKnown, call_in);
    g.set_effect_input(call, start);
    auto ret = b.return_node(call);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, call);

    ICStubEmissionPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();

    // No CheckClass should be inserted for CallKnown.
    EXPECT_EQ(count_live(g, NodeKind::CheckClass), 0)
        << "ICStubEmission should NOT insert CheckClass for CallKnown";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: all final passes run together
// ═══════════════════════════════════════════════════════════════════════════════

TEST(FinalPassesIntegrationTest, AllPassesRunOnLoopWithCallVirt) {
    // A graph with a loop, a CallVirt, and a Throw — exercises all passes.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();

    // CallVirt on a fresh object.
    auto newobj = b.allocate(ShapeId{1});
    g.set_effect_input(newobj, start);
    NodeId callvirt_in[] = {newobj};
    auto callvirt = g.create(NodeKind::CallVirt, callvirt_in);
    g.set_effect_input(callvirt, newobj);

    // Loop: i=0; while(i<5) i++
    auto zero = b.const_int(0);
    auto st_i0 = g.create(NodeKind::StLoc);
    NodeId si[] = {zero}; g.set_data_inputs(st_i0, si);
    g.set_effect_input(st_i0, callvirt);
    g.side(st_i0).class_id = 0;

    auto loop_hdr = g.create(NodeKind::Loop);
    g.set_ctrl_input(loop_hdr, start);
    g.set_effect_input(loop_hdr, st_i0);

    auto ld_i = g.create(NodeKind::LdLoc);
    g.side(ld_i).class_id = 0;
    auto five = b.const_int(5);
    NodeId lt_in[] = {ld_i, five};
    auto lt = g.create(NodeKind::Lt, lt_in);
    auto if_node = b.if_node(lt);
    g.set_ctrl_input(if_node, loop_hdr);
    g.set_effect_input(if_node, loop_hdr);

    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    auto ret = b.return_node(zero);
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
    NodeId si2[] = {add}; g.set_data_inputs(st_i, si2);
    g.set_ctrl_input(st_i, iftrue);
    g.set_effect_input(st_i, iftrue);
    g.side(st_i).class_id = 0;
    auto jump = g.create(NodeKind::Jump);
    g.set_ctrl_input(jump, st_i);
    g.set_effect_input(jump, st_i);

    // Run all final passes.
    PassContext ctx;
    LoopUnrollingPass lu; auto r1 = lu.run(g, ctx); ASSERT_TRUE(r1.has_value());
    ProfileGuidedBlockReorderPass pgbr; auto r2 = pgbr.run(g, ctx); ASSERT_TRUE(r2.has_value());
    ICStubEmissionPass icse; auto r3 = icse.run(g, ctx); ASSERT_TRUE(r3.has_value());

    SUCCEED() << "All final passes ran successfully";
}
