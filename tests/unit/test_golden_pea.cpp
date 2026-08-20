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
