// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_pea_java.cpp
//
// Tests PEA on Java bytecode patterns. The JVM lowerer produces SoN IR
// from JVM bytecode; PEA then optimizes it. These tests verify that PEA
// correctly eliminates allocations in common Java patterns.

#include <gtest/gtest.h>
#include "jade/jvm/Lowerer.hpp"
#include "jade/jvm/Opcode.hpp"
#include "jade/ir/Graph.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/Verifier.hpp"
#include "jade/tier3_diamond/PEA.hpp"
#include "jade/ir/passes/DeadCodeElimination.hpp"

#include <vector>
#include <cstdint>

using namespace jade;
using namespace jade::jvm;
using namespace jade::tier3;

namespace {

class JvmEncoder {
public:
    JvmEncoder& emit1(uint8_t op)               { bytes_.push_back(op); return *this; }
    JvmEncoder& emit_u1(uint8_t op, uint8_t v)  {
        bytes_.push_back(op);
        bytes_.push_back(v);
        return *this;
    }
    JvmEncoder& emit_u2(uint8_t op, uint16_t v) {
        bytes_.push_back(op);
        bytes_.push_back(static_cast<uint8_t>(v & 0xFF));
        bytes_.push_back(static_cast<uint8_t>(v >> 8));
        return *this;
    }
    JvmEncoder& emit_s1(uint8_t op, int8_t v)  { return emit_u1(op, static_cast<uint8_t>(v)); }
    JvmEncoder& emit_s2(uint8_t op, int16_t v)  { return emit_u2(op, static_cast<uint16_t>(v)); }
    [[nodiscard]] std::vector<uint8_t> build() const { return bytes_; }
private:
    std::vector<uint8_t> bytes_;
};

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
// Java pattern 1: new + putfield + getfield + ireturn (NoEscape)
//
// JVM bytecode:
//   new          # push new Object
//   dup          # dup for putfield
//   bipush 42    # push 42
//   putfield     # obj.field = 42
//   getfield     # load obj.field → 42
//   ireturn      # return 42
//
// The allocation never escapes (the return value is the field, not the object).
// PEA should eliminate the NewObj, StFld, and LdFld.
// ═══════════════════════════════════════════════════════════════════════════════

TEST(PeaJavaTest, NewPutfieldGetfieldIreturnNoEscape) {
    auto bytes = JvmEncoder()
                     .emit_u2(0xBB, 0x0002)    // new
                     .emit1(0x59)               // dup
                     .emit_s1(0x10, 42)         // bipush 42
                     .emit_u2(0xB5, 0x0004)     // putfield
                     .emit_u2(0xB4, 0x0004)     // getfield
                     .emit1(0xAC)               // ireturn
                     .build();

    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 2, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();

    // Verify the lowered graph has a NewObj before PEA.
    ASSERT_GE(count_live(*r, NodeKind::NewObj), 1);

    // Run PEA + DCE.
    PEAPass pea;
    PassContext ctx;
    auto pr = pea.run(*r, ctx);
    ASSERT_TRUE(pr.has_value()) << pr.error().what();

    DeadCodeEliminationPass dce;
    auto dr = dce.run(*r, ctx);
    ASSERT_TRUE(dr.has_value()) << dr.error().what();

    // After PEA + DCE: NewObj, StFld, LdFld should all be dead.
    EXPECT_EQ(count_live(*r, NodeKind::NewObj), 0)
        << "NewObj must be eliminated (NoEscape)";
    EXPECT_EQ(count_live(*r, NodeKind::StFld), 0)
        << "StFld must be eliminated (dead store after SRA)";
    EXPECT_EQ(count_live(*r, NodeKind::LdFld), 0)
        << "LdFld must be eliminated (forwarded by SRA)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Java pattern 2: new + areturn (GlobalEscape — alloc kept)
//
// JVM bytecode:
//   new          # push new Object
//   areturn      # return the object (escapes)
//
// The ONLY use of NewObj is the Return (escaping). classify_escape sees
// escape_count == total_uses → GlobalEscape. PEA does NOT optimize —
// the allocation must happen anyway because it escapes on ALL paths.
// No Materialize is inserted (Materialize is only for PartialEscape).
// ═══════════════════════════════════════════════════════════════════════════════

TEST(PeaJavaTest, NewAreturnGlobalEscapeKept) {
    auto bytes = JvmEncoder()
                     .emit_u2(0xBB, 0x0002)    // new
                     .emit1(0xB0)               // areturn
                     .build();

    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 2, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();

    ASSERT_GE(count_live(*r, NodeKind::NewObj), 1);

    PEAPass pea;
    PassContext ctx;
    auto pr = pea.run(*r, ctx);
    ASSERT_TRUE(pr.has_value()) << pr.error().what();

    // GlobalEscape: all uses escape → PEA keeps the allocation.
    // Find the NewObj node and verify it's still alive.
    NodeId newobj_id = NodeId::invalid();
    for (std::size_t i = 0; i < r->size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        if (!r->node(id).is_dead() && r->node(id).kind == NodeKind::NewObj) {
            newobj_id = id;
            break;
        }
    }
    ASSERT_TRUE(newobj_id.valid());
    EXPECT_FALSE(r->node(newobj_id).is_dead())
        << "GlobalEscape: NewObj must be kept (all uses escape)";
    EXPECT_EQ(count_live(*r, NodeKind::Materialize), 0)
        << "No Materialize for GlobalEscape (alloc happens anyway)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Java pattern 3: newarray + arraylength (NoEscape, but PEA doesn't forward
// ArrayLength yet — documents the limitation)
//
// JVM bytecode:
//   bipush 5     # push length 5
//   newarray     # new int[5]
//   arraylength  # load array length → 5
//   ireturn      # return 5
//
// The array never escapes. However, PEA's SRA only forwards LdFld/LoadField
// — it doesn't recognize `arraylength(newarray(len)) → len`. This is a
// pattern-match optimization (like Box→Unbox) that's future work.
// ═══════════════════════════════════════════════════════════════════════════════

TEST(PeaJavaTest, NewarrayArraylengthLimitation) {
    auto bytes = JvmEncoder()
                     .emit_s1(0x10, 5)          // bipush 5
                     .emit1(0xBC)                // newarray (int)
                     .emit1(0xBE)                // arraylength
                     .emit1(0xAC)                // ireturn
                     .build();

    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 2, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();

    ASSERT_GE(count_live(*r, NodeKind::NewArr), 1);

    PEAPass pea;
    PassContext ctx;
    auto pr = pea.run(*r, ctx);
    ASSERT_TRUE(pr.has_value()) << pr.error().what();

    // Limitation: PEA doesn't forward ArrayLength → NewArr stays alive.
    // (The arraylength(newarray(len)) → len pattern match is future work.)
    EXPECT_GE(count_live(*r, NodeKind::NewArr), 1)
        << "Limitation: PEA doesn't forward ArrayLength yet";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Java pattern 4: new + putfield + areturn (partial escape)
//
// JVM bytecode:
//   new          # push new Object
//   dup          # dup for putfield
//   bipush 42    # push 42
//   putfield     # obj.field = 42
//   areturn      # return obj (escapes)
//
// PEA should insert a Materialize at the areturn and eliminate the NewObj.
// ═══════════════════════════════════════════════════════════════════════════════

TEST(PeaJavaTest, NewPutfieldAreturnPartialEscape) {
    auto bytes = JvmEncoder()
                     .emit_u2(0xBB, 0x0002)    // new
                     .emit1(0x59)               // dup
                     .emit_s1(0x10, 42)         // bipush 42
                     .emit_u2(0xB5, 0x0004)     // putfield
                     .emit1(0xB0)               // areturn (return obj — escapes)
                     .build();

    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 2, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();

    ASSERT_GE(count_live(*r, NodeKind::NewObj), 1);

    PEAPass pea;
    PassContext ctx;
    auto pr = pea.run(*r, ctx);
    ASSERT_TRUE(pr.has_value()) << pr.error().what();

    DeadCodeEliminationPass dce;
    auto dr = dce.run(*r, ctx);
    ASSERT_TRUE(dr.has_value()) << dr.error().what();

    // Partial escape: the obj escapes via areturn, but the putfield is
    // non-escaping. PEA should insert a Materialize and eliminate NewObj.
    EXPECT_EQ(count_live(*r, NodeKind::NewObj), 0)
        << "NewObj must be eliminated (materialized at escape point)";
    EXPECT_GE(count_live(*r, NodeKind::Materialize), 1)
        << "A Materialize must be inserted for the escaping areturn";
}
