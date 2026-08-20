// SPDX-License-Identifier: MIT
// .JADE Compiler — tests/unit/test_pea_materialize.cpp
//
// Tests for PEA with per-block escape analysis and materialization.

#include <gtest/gtest.h>
#include "jade/ir/Graph.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"
#include "jade/ir/passes/PassPipeline.hpp"
#include "jade/tier3_diamond/PEA.hpp"
#include "jade/tier3_diamond/SRA.hpp"

using namespace jade;
using namespace jade::tier3;

// Helper: find the return node's data input.
static NodeId get_return_value(const Graph& g) {
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        if (g.node(id).kind == NodeKind::Return) {
            auto inputs = g.data_inputs(id);
            return inputs.empty() ? NodeId::invalid() : inputs[0];
        }
    }
    return NodeId::invalid();
}

// ── NoEscape: allocation + stores + loads all eliminated ──

TEST(PEAMaterializeTest, NoEscapeAllocationEliminated) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto val = b.const_int(42);
    auto sf = b.store_field(alloc, StringId{1}, 0, val);
    g.set_effect_input(sf, alloc);
    auto lf = b.load_field(alloc, StringId{1}, 0);
    g.set_effect_input(lf, sf);
    auto ret = b.return_node(lf);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, lf);

    PEAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // Allocation should be eliminated.
    EXPECT_TRUE(g.node(alloc).is_dead());
    // Load should be eliminated (replaced with a new ConstInt).
    EXPECT_TRUE(g.node(lf).is_dead());
    // The return value should be a ConstInt with value 42.
    NodeId ret_val = get_return_value(g);
    ASSERT_TRUE(ret_val.valid());
    EXPECT_TRUE(g.node(ret_val).is_const());
    EXPECT_EQ(g.side(ret_val).const_value.i64, 42);
}

// ── GlobalEscape: allocation kept ──

TEST(PEAMaterializeTest, PartialEscapeViaReturnMaterialized) {
    // alloc + store + return(alloc): the Return escapes, the StoreField
    // doesn't. PEA classifies this as PartialEscape, inserts a Materialize
    // at the Return, and eliminates the original Allocate.
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto val = b.const_int(42);
    auto sf = b.store_field(alloc, StringId{1}, 0, val);
    g.set_effect_input(sf, alloc);
    auto ret = b.return_node(alloc);  // escapes via return
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, sf);

    PEAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // The Allocate is eliminated — PEA inserted a Materialize for the escape.
    EXPECT_TRUE(g.node(alloc).is_dead());
    // A Materialize was inserted.
    bool found_mat = false;
    for (std::size_t i = 0; i < g.size(); ++i) {
        if (!g.node(NodeId{uint32_t(i+1)}).is_dead() &&
            g.node(NodeId{uint32_t(i+1)}).kind == NodeKind::Materialize) {
            found_mat = true;
            break;
        }
    }
    EXPECT_TRUE(found_mat) << "PEA must insert a Materialize for the escaping Return";
}

// ── PartialEscape: load forwarded, allocation kept for escape ──

TEST(PEAMaterializeTest, PartialEscapeLoadForwardedAndMaterializeInserted) {
    // The allocation escapes via Return, but the field load is non-escaping.
    // PEA should:
    //   - Forward the load (eliminate LdFld).
    //   - Insert a Materialize at the Return's escape point.
    //   - Rewire the Return to read the Materialize instead of the alloc.
    //   - Eliminate the original Allocate (it's now dead).
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto val = b.const_int(42);
    auto sf = b.store_field(alloc, StringId{1}, 0, val);
    g.set_effect_input(sf, alloc);
    auto lf = b.load_field(alloc, StringId{1}, 0);
    g.set_effect_input(lf, sf);
    // Return the allocation (escapes) but also load its field (non-escaping).
    auto ret = b.return_node(alloc);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, lf);

    PEAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // The load should be eliminated (forwarded).
    EXPECT_TRUE(g.node(lf).is_dead());
    // The original allocation should be dead — its only escaping use (the
    // Return) was rewired to a Materialize node.
    EXPECT_TRUE(g.node(alloc).is_dead());
    // A Materialize node should have been inserted.
    bool found_materialize = false;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        if (g.node(id).kind == NodeKind::Materialize) {
            found_materialize = true;
            break;
        }
    }
    EXPECT_TRUE(found_materialize) << "PEA should insert a Materialize node";
}

// ── Box elimination: Box(v) that never escapes ──

TEST(PEAMaterializeTest, BoxEliminationNoEscape) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    NodeId inputs[] = {val};
    auto box = g.create(NodeKind::Box, inputs);
    g.set_effect_input(box, start);
    auto ret = b.return_node(val);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, box);

    PEAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(box).is_dead());
}

// ── Box that escapes: kept ──

TEST(PEAMaterializeTest, BoxEscapeKept) {
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
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_FALSE(g.node(box).is_dead());
}

// ── Multiple fields: SRA forwards each independently ──

TEST(PEAMaterializeTest, MultipleFieldsForwarded) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto v1 = b.const_int(10);
    auto v2 = b.const_int(20);
    auto sf1 = b.store_field(alloc, StringId{1}, 0, v1);
    g.set_effect_input(sf1, alloc);
    auto sf2 = b.store_field(alloc, StringId{2}, 8, v2);
    g.set_effect_input(sf2, sf1);
    auto lf1 = b.load_field(alloc, StringId{1}, 0);
    g.set_effect_input(lf1, sf2);
    auto lf2 = b.load_field(alloc, StringId{2}, 8);
    g.set_effect_input(lf2, lf1);
    auto add = b.add(lf1, lf2);
    auto ret = b.return_node(add);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, lf2);

    PEAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // Both loads should be eliminated (forwarded to constants).
    EXPECT_TRUE(g.node(lf1).is_dead());
    EXPECT_TRUE(g.node(lf2).is_dead());
    // The allocation should be eliminated.
    EXPECT_TRUE(g.node(alloc).is_dead());
}

// ── Iterative fixpoint: eliminating one allocation enables another ──

TEST(PEAMaterializeTest, IterativeFixpoint) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc1 = b.allocate(ShapeId{1});
    g.set_effect_input(alloc1, start);
    auto alloc2 = b.allocate(ShapeId{2});
    g.set_effect_input(alloc2, alloc1);
    auto v = b.const_int(42);
    auto sf1 = b.store_field(alloc1, StringId{1}, 0, v);
    g.set_effect_input(sf1, alloc2);
    auto lf1 = b.load_field(alloc1, StringId{1}, 0);
    g.set_effect_input(lf1, sf1);
    auto sf2 = b.store_field(alloc2, StringId{1}, 0, lf1);
    g.set_effect_input(sf2, lf1);
    auto lf2 = b.load_field(alloc2, StringId{1}, 0);
    g.set_effect_input(lf2, sf2);
    auto ret = b.return_node(lf2);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, lf2);

    PEAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // Both allocations should be eliminated after iterative fixpoint.
    EXPECT_TRUE(g.node(alloc1).is_dead());
    EXPECT_TRUE(g.node(alloc2).is_dead());
    // The return value should be a ConstInt with value 42.
    NodeId ret_val = get_return_value(g);
    ASSERT_TRUE(ret_val.valid());
    EXPECT_TRUE(g.node(ret_val).is_const());
    EXPECT_EQ(g.side(ret_val).const_value.i64, 42);
}

// ── DIAMOND pipeline: PEA + SRA + DCE work together ──

TEST(PEAMaterializeTest, DiamondPipelineEliminatesAllocations) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto v = b.const_int(99);
    auto sf = b.store_field(alloc, StringId{1}, 0, v);
    g.set_effect_input(sf, alloc);
    auto lf = b.load_field(alloc, StringId{1}, 0);
    g.set_effect_input(lf, sf);
    auto ret = b.return_node(lf);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, lf);

    PassContext ctx;
    auto pipe = build_diamond_pipeline();
    auto r = pipe->run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // The allocation should be eliminated.
    EXPECT_TRUE(g.node(alloc).is_dead());
    // The return value should be 99.
    NodeId ret_val = get_return_value(g);
    ASSERT_TRUE(ret_val.valid());
    EXPECT_TRUE(g.node(ret_val).is_const());
    EXPECT_EQ(g.side(ret_val).const_value.i64, 99);
}

// ── Store then load with non-constant value: rewired ──

TEST(PEAMaterializeTest, NonConstantStoreLoadRewired) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto phi = g.create(NodeKind::Phi);
    auto sf = b.store_field(alloc, StringId{1}, 0, phi);
    g.set_effect_input(sf, alloc);
    auto lf = b.load_field(alloc, StringId{1}, 0);
    g.set_effect_input(lf, sf);
    auto ret = b.return_node(lf);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, lf);

    PEAPass pass;
    PassContext ctx;
    auto r = pass.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(g.node(lf).is_dead());
    EXPECT_TRUE(g.node(alloc).is_dead());
    // The return value should be the Phi.
    NodeId ret_val = get_return_value(g);
    ASSERT_TRUE(ret_val.valid());
    EXPECT_EQ(ret_val, phi);
}
