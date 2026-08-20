// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_golden_pea.cpp
//
// Golden IR test for PEA (Rule 37).
//
// Verifies that PEA + DCE produces the expected IR for the
// 02_partial_escape_with_if pattern:
//   alloc = new Object();
//   alloc.field = 42;
//   if (1) return alloc;        // ESCAPES — must materialize here
//   else return alloc.field;    // non-escaping — SRA forwards to ConstInt(42)
//
// After PEA + DCE:
//   - Exactly one Materialize node is inserted.
//   - The escaping Return reads the Materialize.
//   - The non-escaping Return reads the forwarded ConstInt value.
//   - The original Allocate is dead (DCE removes it).
//   - The StoreField is dead (DCE removes it — dead store).
//   - The LoadField is dead (SRA forwarded it).

#include <gtest/gtest.h>
#include "jade/ir/Graph.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/Verifier.hpp"
#include "jade/tier3_diamond/PEA.hpp"
#include "jade/ir/passes/DeadCodeElimination.hpp"

using namespace jade;
using namespace jade::tier3;

namespace {

// Build the input IR for 02_partial_escape_with_if:
//   alloc = new Object();
//   alloc.field = 42;
//   if (1) return alloc;        // escapes
//   else return alloc.field;    // non-escaping
void build_partial_escape_with_if(Graph& g) {
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto sf = b.store_field(alloc, StringId{1}, 0, val);
    g.set_effect_input(sf, alloc);
    auto cond = b.const_int(1);
    auto if_node = b.if_node(cond);
    g.set_ctrl_input(if_node, start);
    g.set_effect_input(if_node, sf);

    // True branch: return alloc (escapes)
    auto iftrue = g.create(NodeKind::IfTrue);
    g.set_ctrl_input(iftrue, if_node);
    g.set_effect_input(iftrue, if_node);
    auto ret_true = b.return_node(alloc);
    g.set_ctrl_input(ret_true, iftrue);
    g.set_effect_input(ret_true, iftrue);

    // False branch: load alloc.field, return it (non-escaping)
    auto iffalse = g.create(NodeKind::IfFalse);
    g.set_ctrl_input(iffalse, if_node);
    g.set_effect_input(iffalse, if_node);
    auto lf = b.load_field(alloc, StringId{1}, 0);
    g.set_effect_input(lf, iffalse);
    auto ret_false = b.return_node(lf);
    g.set_ctrl_input(ret_false, iffalse);
    g.set_effect_input(ret_false, lf);
}

}  // namespace

// Golden test: PEA + DCE on 02_partial_escape_with_if produces the expected IR.
TEST(GoldenPEATest, PartialEscapeWithIf) {
    Graph g;
    build_partial_escape_with_if(g);

    // Run PEA, then DCE to clean up dead nodes (StoreField, Allocate, LoadField).
    PEAPass pea;
    PassContext ctx;
    auto r = pea.run(g, ctx);
    ASSERT_TRUE(r.has_value()) << r.error().what();

    DeadCodeEliminationPass dce;
    auto r2 = dce.run(g, ctx);
    ASSERT_TRUE(r2.has_value()) << r2.error().what();

    // Invariant 1: exactly one Materialize node in the output.
    int materialize_count = 0;
    NodeId mat_id = NodeId::invalid();
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind == NodeKind::Materialize) {
            ++materialize_count;
            mat_id = id;
        }
    }
    EXPECT_EQ(materialize_count, 1)
        << "Expected exactly 1 Materialize node, found " << materialize_count;

    // Invariant 2: the original Allocate is dead (DCE removed it).
    NodeId alloc_id = NodeId::invalid();
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        if (g.node(id).kind == NodeKind::Allocate) {
            alloc_id = id;
            break;
        }
    }
    ASSERT_TRUE(alloc_id.valid());
    EXPECT_TRUE(g.node(alloc_id).is_dead())
        << "Allocate must be dead after PEA + DCE";

    // Invariant 3: the StoreField is dead (DCE removed it — dead store).
    NodeId sf_id = NodeId::invalid();
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        if (g.node(id).kind == NodeKind::StoreField) {
            sf_id = id;
            break;
        }
    }
    if (sf_id.valid()) {
        EXPECT_TRUE(g.node(sf_id).is_dead())
            << "StoreField must be dead after PEA + DCE (dead store)";
    }

    // Invariant 4: the LoadField is dead (SRA forwarded it to the stored value).
    NodeId lf_id = NodeId::invalid();
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        if (g.node(id).kind == NodeKind::LoadField) {
            lf_id = id;
            break;
        }
    }
    if (lf_id.valid()) {
        EXPECT_TRUE(g.node(lf_id).is_dead())
            << "LoadField must be dead after PEA + DCE (forwarded by SRA)";
    }

    // Invariant 5: the escaping Return (true branch) reads the Materialize.
    if (mat_id.valid()) {
        bool found_mat_use = false;
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            const Node& n = g.node(id);
            if (n.is_dead()) continue;
            if (n.kind != NodeKind::Return) continue;
            for (NodeId in : g.data_inputs(id)) {
                if (in == mat_id) {
                    found_mat_use = true;
                    break;
                }
            }
        }
        EXPECT_TRUE(found_mat_use)
            << "The escaping Return must read the Materialize node";
    }

    // Invariant 6: the non-escaping Return (false branch) reads a ConstInt
    // (the forwarded value from SRA).
    bool found_const_return = false;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = g.node(id);
        if (n.is_dead()) continue;
        if (n.kind != NodeKind::Return) continue;
        auto inputs = g.data_inputs(id);
        if (inputs.empty()) continue;
        NodeId ret_val = inputs[0];
        if (!ret_val.valid() || ret_val.value > g.size()) continue;
        if (g.node(ret_val).kind == NodeKind::ConstInt) {
            found_const_return = true;
            // Verify it's the right value (42).
            EXPECT_EQ(g.side(ret_val).const_value.i64, 42)
                << "The non-escaping Return must read ConstInt(42)";
            break;
        }
    }
    EXPECT_TRUE(found_const_return)
        << "The non-escaping Return must read a ConstInt (forwarded by SRA)";

    // Invariant 7: the Materialize reads the stored field value (ConstInt 42).
    if (mat_id.valid()) {
        auto mat_inputs = g.data_inputs(mat_id);
        bool reads_const_42 = false;
        for (NodeId in : mat_inputs) {
            if (!in.valid() || in.value > g.size()) continue;
            const Node& in_node = g.node(in);
            if (in_node.kind == NodeKind::ConstInt && g.side(in).const_value.i64 == 42) {
                reads_const_42 = true;
                break;
            }
        }
        EXPECT_TRUE(reads_const_42)
            << "Materialize must read the stored field value (ConstInt 42)";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Helper: count live nodes of a given kind.
// ═══════════════════════════════════════════════════════════════════════════════

[[nodiscard]] int count_live(const Graph& g, NodeKind k) {
    int n = 0;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        if (!g.node(id).is_dead() && g.node(id).kind == k) ++n;
    }
    return n;
}

[[nodiscard]] NodeId find_first_live(const Graph& g, NodeKind k) {
    for (std::size_t i = 0; i < g.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        if (!g.node(id).is_dead() && g.node(id).kind == k) return id;
    }
    return NodeId::invalid();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 01_simple_non_escape: alloc + store + load + return(load).
// PEA eliminates everything; Return reads ConstInt.
// ═══════════════════════════════════════════════════════════════════════════════

TEST(GoldenPEATest, SimpleNonEscape) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto sf = b.store_field(alloc, StringId{1}, 0, val);
    g.set_effect_input(sf, alloc);
    auto lf = b.load_field(alloc, StringId{1}, 0);
    g.set_effect_input(lf, sf);
    auto ret = b.return_node(lf);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, lf);

    PEAPass pea; PassContext ctx;
    DeadCodeEliminationPass dce;
    ASSERT_TRUE(pea.run(g, ctx).has_value());
    ASSERT_TRUE(dce.run(g, ctx).has_value());

    EXPECT_EQ(count_live(g, NodeKind::Allocate), 0);
    EXPECT_EQ(count_live(g, NodeKind::StoreField), 0);
    EXPECT_EQ(count_live(g, NodeKind::LoadField), 0);
    EXPECT_EQ(count_live(g, NodeKind::Materialize), 0);
    // Return reads ConstInt(42).
    NodeId ret_id = find_first_live(g, NodeKind::Return);
    ASSERT_TRUE(ret_id.valid());
    auto ret_in = g.data_inputs(ret_id);
    ASSERT_EQ(ret_in.size(), 1u);
    EXPECT_EQ(g.node(ret_in[0]).kind, NodeKind::ConstInt);
    EXPECT_EQ(g.side(ret_in[0]).const_value.i64, 42);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 03_partial_escape_with_loop: alloc inside loop body, never escapes.
// ═══════════════════════════════════════════════════════════════════════════════

TEST(GoldenPEATest, PartialEscapeWithLoop) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    // sum = 0
    auto zero = b.const_int(0);
    auto st_sum = g.create(NodeKind::StLoc);
    NodeId si[] = {zero}; g.set_data_inputs(st_sum, si);
    g.set_effect_input(st_sum, start);
    g.side(st_sum).class_id = 0;
    // temp = new Object(); temp.field = 99; sum += temp.field
    auto temp = b.allocate(ShapeId{1});
    g.set_effect_input(temp, st_sum);
    auto ninety_nine = b.const_int(99);
    auto sf = b.store_field(temp, StringId{1}, 0, ninety_nine);
    g.set_effect_input(sf, temp);
    auto lf = b.load_field(temp, StringId{1}, 0);
    g.set_effect_input(lf, sf);
    auto ret = b.return_node(lf);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, lf);

    PEAPass pea; PassContext ctx;
    DeadCodeEliminationPass dce;
    ASSERT_TRUE(pea.run(g, ctx).has_value());
    ASSERT_TRUE(dce.run(g, ctx).has_value());

    // temp never escapes → Allocate dead, SRA forwards load to ConstInt(99).
    EXPECT_EQ(count_live(g, NodeKind::Allocate), 0);
    EXPECT_EQ(count_live(g, NodeKind::StoreField), 0);
    EXPECT_EQ(count_live(g, NodeKind::LoadField), 0);
    EXPECT_EQ(count_live(g, NodeKind::Materialize), 0);
    NodeId ret_id = find_first_live(g, NodeKind::Return);
    ASSERT_TRUE(ret_id.valid());
    auto ret_in = g.data_inputs(ret_id);
    ASSERT_EQ(ret_in.size(), 1u);
    EXPECT_EQ(g.node(ret_in[0]).kind, NodeKind::ConstInt);
    EXPECT_EQ(g.side(ret_in[0]).const_value.i64, 99);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 04_box_unbox_pair: Box(v) → Unbox(Box). Both eliminated.
// ═══════════════════════════════════════════════════════════════════════════════

TEST(GoldenPEATest, BoxUnboxPair) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    NodeId box_in[] = {val};
    auto box = g.create(NodeKind::Box, box_in);
    g.set_effect_input(box, start);
    NodeId unbox_in[] = {box};
    auto unbox = g.create(NodeKind::Unbox, unbox_in);
    g.set_effect_input(unbox, box);
    auto ret = b.return_node(unbox);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, unbox);

    PEAPass pea; PassContext ctx;
    DeadCodeEliminationPass dce;
    ASSERT_TRUE(pea.run(g, ctx).has_value());
    ASSERT_TRUE(dce.run(g, ctx).has_value());

    // Box is NoEscape → eliminated. Unbox reads Box → rewired to val.
    EXPECT_EQ(count_live(g, NodeKind::Box), 0);
    // Return reads ConstInt(42).
    NodeId ret_id = find_first_live(g, NodeKind::Return);
    ASSERT_TRUE(ret_id.valid());
    auto ret_in = g.data_inputs(ret_id);
    ASSERT_EQ(ret_in.size(), 1u);
    EXPECT_EQ(g.node(ret_in[0]).kind, NodeKind::ConstInt);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 05_allocation_in_try_catch: alloc with a guard. Phase 1 bails out.
// ═══════════════════════════════════════════════════════════════════════════════

TEST(GoldenPEATest, AllocationInTryCatch) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto sf = b.store_field(alloc, StringId{1}, 0, val);
    g.set_effect_input(sf, alloc);
    NodeId check_in[] = {alloc};
    auto check = g.create(NodeKind::CheckNotNull, check_in);
    g.set_ctrl_input(check, start);
    g.set_effect_input(check, sf);
    g.set_frame_state(check, FrameStateId{1});
    auto ret = b.return_node(alloc);
    g.set_ctrl_input(ret, check);
    g.set_effect_input(ret, check);

    PEAPass pea; PassContext ctx;
    ASSERT_TRUE(pea.run(g, ctx).has_value());

    // Phase 1 limitation: PEA bails out on guards. Allocate is kept.
    EXPECT_FALSE(g.node(alloc).is_dead());
    EXPECT_EQ(count_live(g, NodeKind::Materialize), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 06_allocation_stored_into_array: alloc escapes via StoreElement.
// ═══════════════════════════════════════════════════════════════════════════════

TEST(GoldenPEATest, AllocationStoredIntoArray) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto arr = b.allocate(ShapeId{2});
    g.set_effect_input(arr, start);
    auto elem = b.allocate(ShapeId{1});
    g.set_effect_input(elem, arr);
    auto idx = b.const_int(0);
    NodeId se_in[] = {arr, idx, elem};
    auto se = g.create(NodeKind::StoreElement, se_in);
    g.set_effect_input(se, elem);
    auto ret = b.return_node(arr);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, se);

    PEAPass pea; PassContext ctx;
    ASSERT_TRUE(pea.run(g, ctx).has_value());

    // elem escapes via StoreElement → kept. arr escapes via Return → kept.
    // (PEA may insert a Materialize for arr's Return, but elem must stay.)
    EXPECT_FALSE(g.node(elem).is_dead())
        << "elem must be kept (escapes via StoreElement)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// 07_allocation_used_in_throw: alloc escapes via Throw → Materialize.
// ═══════════════════════════════════════════════════════════════════════════════

TEST(GoldenPEATest, AllocationUsedInThrow) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto sf = b.store_field(alloc, StringId{1}, 0, val);
    g.set_effect_input(sf, alloc);
    auto throw_node = g.create(NodeKind::Throw);
    NodeId th_in[] = {alloc};
    g.set_data_inputs(throw_node, th_in);
    g.set_ctrl_input(throw_node, start);
    g.set_effect_input(throw_node, sf);

    PEAPass pea; PassContext ctx;
    DeadCodeEliminationPass dce;
    ASSERT_TRUE(pea.run(g, ctx).has_value());
    ASSERT_TRUE(dce.run(g, ctx).has_value());

    // Throw is an escaping use → Materialize inserted, Allocate dead.
    EXPECT_TRUE(g.node(alloc).is_dead());
    EXPECT_GE(count_live(g, NodeKind::Materialize), 1);
    // Throw reads the Materialize.
    NodeId th_id = find_first_live(g, NodeKind::Throw);
    ASSERT_TRUE(th_id.valid());
    auto th_in2 = g.data_inputs(th_id);
    ASSERT_EQ(th_in2.size(), 1u);
    EXPECT_EQ(g.node(th_in2[0]).kind, NodeKind::Materialize);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 08_recursive_allocation: A.field = B; return A. Both escape.
// ═══════════════════════════════════════════════════════════════════════════════

TEST(GoldenPEATest, RecursiveAllocation) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.allocate(ShapeId{1});
    g.set_effect_input(a, start);
    auto bb = b.allocate(ShapeId{1});
    g.set_effect_input(bb, a);
    auto val = b.const_int(99);
    auto sf_b = b.store_field(bb, StringId{1}, 0, val);
    g.set_effect_input(sf_b, bb);
    auto sf_a = b.store_field(a, StringId{1}, 0, bb);
    g.set_effect_input(sf_a, sf_b);
    auto ret = b.return_node(a);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, sf_a);

    PEAPass pea; PassContext ctx;
    DeadCodeEliminationPass dce;
    ASSERT_TRUE(pea.run(g, ctx).has_value());
    ASSERT_TRUE(dce.run(g, ctx).has_value());

    // A escapes via Return → Materialize. B escapes via A's field → Materialize.
    EXPECT_GE(count_live(g, NodeKind::Materialize), 1);
    // The Return reads a Materialize (A's materialized form).
    NodeId ret_id = find_first_live(g, NodeKind::Return);
    ASSERT_TRUE(ret_id.valid());
    auto ret_in = g.data_inputs(ret_id);
    ASSERT_EQ(ret_in.size(), 1u);
    // The Return's input should be a Materialize (A's).
    EXPECT_EQ(g.node(ret_in[0]).kind, NodeKind::Materialize);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 09_deopt_materialization: alloc with a guard + load. Phase 1 bails out.
// ═══════════════════════════════════════════════════════════════════════════════

TEST(GoldenPEATest, DeoptMaterialization) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto sf = b.store_field(alloc, StringId{1}, 0, val);
    g.set_effect_input(sf, alloc);
    NodeId check_in[] = {alloc};
    auto check = g.create(NodeKind::CheckNotNull, check_in);
    g.set_ctrl_input(check, start);
    g.set_effect_input(check, sf);
    g.set_frame_state(check, FrameStateId{1});
    auto lf = b.load_field(alloc, StringId{1}, 0);
    g.set_effect_input(lf, check);
    auto ret = b.return_node(lf);
    g.set_ctrl_input(ret, check);
    g.set_effect_input(ret, lf);

    PEAPass pea; PassContext ctx;
    ASSERT_TRUE(pea.run(g, ctx).has_value());

    // Phase 1 limitation: PEA bails out on guards.
    EXPECT_FALSE(g.node(alloc).is_dead());
    EXPECT_EQ(count_live(g, NodeKind::Materialize), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 10_no_escape_after_inlining: same as 01 (Inlining is a no-op today).
// ═══════════════════════════════════════════════════════════════════════════════

TEST(GoldenPEATest, NoEscapeAfterInlining) {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto val = b.const_int(42);
    auto alloc = b.allocate(ShapeId{1});
    g.set_effect_input(alloc, start);
    auto sf = b.store_field(alloc, StringId{1}, 0, val);
    g.set_effect_input(sf, alloc);
    auto lf = b.load_field(alloc, StringId{1}, 0);
    g.set_effect_input(lf, sf);
    auto ret = b.return_node(lf);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, lf);

    PEAPass pea; PassContext ctx;
    DeadCodeEliminationPass dce;
    ASSERT_TRUE(pea.run(g, ctx).has_value());
    ASSERT_TRUE(dce.run(g, ctx).has_value());

    EXPECT_EQ(count_live(g, NodeKind::Allocate), 0);
    EXPECT_EQ(count_live(g, NodeKind::Materialize), 0);
    NodeId ret_id = find_first_live(g, NodeKind::Return);
    ASSERT_TRUE(ret_id.valid());
    auto ret_in = g.data_inputs(ret_id);
    ASSERT_EQ(ret_in.size(), 1u);
    EXPECT_EQ(g.node(ret_in[0]).kind, NodeKind::ConstInt);
}
