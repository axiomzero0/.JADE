// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_jvm_lowerer.cpp
//
// Tests for the JVM bytecode → SoN IR lowering.

#include <gtest/gtest.h>
#include "jade/jvm/Lowerer.hpp"
#include "jade/jvm/Opcode.hpp"
#include "jade/ir/Graph.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/Verifier.hpp"
#include "jade/ir/passes/ConstantFolding.hpp"

#include <vector>
#include <cstdint>

using namespace jade;
using namespace jade::jvm;

namespace {

class JvmEncoder {
public:
    JvmEncoder& emit1(uint8_t op)               { bytes_.push_back(op); return *this; }
    JvmEncoder& emit_u1(uint8_t op, uint8_t v)  {
        bytes_.push_back(op);
        bytes_.push_back(v);
        return *this;
    }
    JvmEncoder& emit_s1(uint8_t op, int8_t v) {
        return emit_u1(op, static_cast<uint8_t>(v));
    }
    JvmEncoder& emit_u2(uint8_t op, uint16_t v) {
        bytes_.push_back(op);
        bytes_.push_back(static_cast<uint8_t>(v >> 8));
        bytes_.push_back(static_cast<uint8_t>(v & 0xFF));
        return *this;
    }
    JvmEncoder& emit_s2(uint8_t op, int16_t v) {
        return emit_u2(op, static_cast<uint16_t>(v));
    }
    JvmEncoder& emit_raw(uint8_t b) { bytes_.push_back(b); return *this; }

    [[nodiscard]] std::vector<uint8_t> build() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};

}  // namespace

TEST(JvmLowererTest, Iconst5AndReturnProducesValidGraph) {
    // iconst_5; ireturn
    auto bytes = JvmEncoder().emit1(0x08).emit1(0xAC).build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 0, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_GT(r->size(), 1u);
}

TEST(JvmLowererTest, IaddOfTwoConstantsProducesAddNode) {
    // iconst_3; iconst_4; iadd; ireturn
    auto bytes = JvmEncoder()
                     .emit1(0x05)   // iconst_2
                     .emit1(0x06)   // iconst_3
                     .emit1(0x60)   // iadd
                     .emit1(0xAC)   // ireturn
                     .build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 0, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    bool found_add = false;
    for (std::size_t i = 0; i < r->size(); ++i) {
        NodeId id{static_cast<uint32_t>(i + 1)};
        if (r->node(id).kind == NodeKind::Add) found_add = true;
    }
    EXPECT_TRUE(found_add);
}

TEST(JvmLowererTest, BipushDecodesAsConstant) {
    // bipush 42; ireturn
    auto bytes = JvmEncoder().emit_s1(0x10, 42).emit1(0xAC).build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 0, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    bool found_const = false;
    for (std::size_t i = 0; i < r->size(); ++i) {
        NodeId id{static_cast<uint32_t>(i + 1)};
        if (r->node(id).kind == NodeKind::ConstInt &&
            r->side(id).const_value.i64 == 42) {
            found_const = true;
        }
    }
    EXPECT_TRUE(found_const);
}

TEST(JvmLowererTest, IloadAndStoreRoundTrip) {
    // iconst_5; istore_0; iload_0; ireturn
    auto bytes = JvmEncoder()
                     .emit1(0x08)   // iconst_5
                     .emit1(0x3B)   // istore_0
                     .emit1(0x1A)   // iload_0
                     .emit1(0xAC)   // ireturn
                     .build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 1, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(verify_graph(*r).has_value());
}

TEST(JvmLowererTest, IincExpandsToAdd) {
    // iconst_0; istore_0; iinc 0 5; iload_0; ireturn
    auto bytes = JvmEncoder()
                     .emit1(0x03)              // iconst_0
                     .emit1(0x3B)              // istore_0
                     .emit_u1(0x84, 0).emit_raw(5)  // iinc 0 5
                     .emit1(0x1A)              // iload_0
                     .emit1(0xAC)              // ireturn
                     .build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 1, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // The iinc expands to: locals[0] = Add(locals[0], ConstInt(5))
    bool found_add = false;
    for (std::size_t i = 0; i < r->size(); ++i) {
        NodeId id{static_cast<uint32_t>(i + 1)};
        if (r->node(id).kind == NodeKind::Add) found_add = true;
    }
    EXPECT_TRUE(found_add);
}

TEST(JvmLowererTest, NewObjIsEffectful) {
    // new <Class 0x0002>; dup; invokespecial <init 0x0003>; areturn
    // (For this test, we just emit `new` and let the lowerer emit NewObj.)
    auto bytes = JvmEncoder()
                     .emit_u2(0xBB, 0x0002)    // new
                     .emit1(0x59)               // dup
                     .emit_u2(0xB7, 0x0003)     // invokespecial
                     .emit1(0xB0)               // areturn
                     .build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 1, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    bool found_newobj = false;
    for (std::size_t i = 0; i < r->size(); ++i) {
        NodeId id{static_cast<uint32_t>(i + 1)};
        if (r->node(id).kind == NodeKind::NewObj) {
            EXPECT_TRUE(r->node(id).is_effect());
            found_newobj = true;
        }
    }
    EXPECT_TRUE(found_newobj);
}

TEST(JvmLowererTest, ArraylengthIsEffectful) {
    // aload_0; arraylength; ireturn
    auto bytes = JvmEncoder()
                     .emit1(0x2A)   // aload_0
                     .emit1(0xBE)   // arraylength
                     .emit1(0xAC)   // ireturn
                     .build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 1, 1);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    bool found_arr_len = false;
    for (std::size_t i = 0; i < r->size(); ++i) {
        NodeId id{static_cast<uint32_t>(i + 1)};
        if (r->node(id).kind == NodeKind::ArrayLength) {
            EXPECT_TRUE(r->node(id).is_effect());
            found_arr_len = true;
        }
    }
    EXPECT_TRUE(found_arr_len);
}

TEST(JvmLowererTest, MonitorenterEmitsMonitorNode) {
    // aload_0; monitorenter; return
    auto bytes = JvmEncoder()
                     .emit1(0x2A)   // aload_0
                     .emit1(0xC2)   // monitorenter
                     .emit1(0xB1)   // return (void)
                     .build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 1, 1);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    bool found_monitor = false;
    for (std::size_t i = 0; i < r->size(); ++i) {
        NodeId id{static_cast<uint32_t>(i + 1)};
        if (r->node(id).kind == NodeKind::MonitorEnter) {
            EXPECT_TRUE(r->node(id).is_effect());
            found_monitor = true;
        }
    }
    EXPECT_TRUE(found_monitor);
}

TEST(JvmLowererTest, CheckcastEmitsCastClass) {
    // aload_0; checkcast <Class 0x0001>; areturn
    auto bytes = JvmEncoder()
                     .emit1(0x2A)
                     .emit_u2(0xC0, 0x0001)
                     .emit1(0xB0)
                     .build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 1, 1);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    bool found_cast = false;
    for (std::size_t i = 0; i < r->size(); ++i) {
        NodeId id{static_cast<uint32_t>(i + 1)};
        if (r->node(id).kind == NodeKind::CastClass) {
            EXPECT_TRUE(r->node(id).is_effect());
            found_cast = true;
        }
    }
    EXPECT_TRUE(found_cast);
}

TEST(JvmLowererTest, AthrowEmitsThrowNode) {
    // new <Exception 0x0001>; dup; invokespecial <init 0x0002>; athrow
    auto bytes = JvmEncoder()
                     .emit_u2(0xBB, 0x0001)
                     .emit1(0x59)
                     .emit_u2(0xB7, 0x0002)
                     .emit1(0xBF)   // athrow
                     .build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 0, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    bool found_throw = false;
    for (std::size_t i = 0; i < r->size(); ++i) {
        NodeId id{static_cast<uint32_t>(i + 1)};
        if (r->node(id).kind == NodeKind::Throw) {
            EXPECT_TRUE(r->node(id).is_effect());
            EXPECT_TRUE(r->node(id).is_control());
            found_throw = true;
        }
    }
    EXPECT_TRUE(found_throw);
}

TEST(JvmLowererTest, ConstantFoldingWorksOnLoweredJVMGraph) {
    // iconst_3; iconst_4; iadd; iconst_5; imul; ireturn
    // Should fold to (3+4)*5 = 35
    auto bytes = JvmEncoder()
                     .emit1(0x05)   // iconst_2
                     .emit1(0x06)   // iconst_3
                     .emit1(0x60)   // iadd  → 5 (but iconst_2+iconst_3=2+3=5)
                     .emit1(0x08)   // iconst_5
                     .emit1(0x68)   // imul
                     .emit1(0xAC)   // ireturn
                     .build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 0, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    Graph& g = *r;
    PassContext ctx;
    ConstantFoldingPass pass;
    ASSERT_TRUE(pass.run(g, ctx).has_value());
    bool found_folded_mul = false;
    for (std::size_t i = 0; i < g.size(); ++i) {
        NodeId id{static_cast<uint32_t>(i + 1)};
        if (g.node(id).kind == NodeKind::Mul && g.node(id).is_const()) {
            EXPECT_EQ(g.side(id).const_value.i64, 25);   // (2+3)*5 = 25
            found_folded_mul = true;
        }
    }
    EXPECT_TRUE(found_folded_mul);
}

TEST(JvmLowererTest, I2lConversionEmitsConvI8) {
    // iconst_5; i2l; lreturn
    auto bytes = JvmEncoder()
                     .emit1(0x08)   // iconst_5
                     .emit1(0x85)   // i2l
                     .emit1(0xAD)   // lreturn
                     .build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 0, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    bool found_conv = false;
    for (std::size_t i = 0; i < r->size(); ++i) {
        NodeId id{static_cast<uint32_t>(i + 1)};
        if (r->node(id).kind == NodeKind::ConvI8) found_conv = true;
    }
    EXPECT_TRUE(found_conv);
}

TEST(JvmLowererTest, EmptyProgramProducesImplicitReturn) {
    std::vector<uint8_t> empty;
    JvmLowerer lowerer;
    auto r = lowerer.lower(empty, 0, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_TRUE(verify_graph(*r).has_value());
}

TEST(JvmLowererTest, GetfieldEmitsLdFld) {
    // aload_0; getfield <Field 0x0001>; ireturn
    auto bytes = JvmEncoder()
                     .emit1(0x2A)
                     .emit_u2(0xB2, 0x0001)
                     .emit1(0xAC)
                     .build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 1, 1);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    bool found_ldfld = false;
    for (std::size_t i = 0; i < r->size(); ++i) {
        NodeId id{static_cast<uint32_t>(i + 1)};
        if (r->node(id).kind == NodeKind::LdFld) {
            EXPECT_TRUE(r->node(id).is_effect());
            found_ldfld = true;
        }
    }
    EXPECT_TRUE(found_ldfld);
}

TEST(JvmLowererTest, DupDuplicatesTopOfStack) {
    // iconst_5; dup; iadd; ireturn  → 5+5 = 10
    auto bytes = JvmEncoder()
                     .emit1(0x08)   // iconst_5
                     .emit1(0x59)   // dup
                     .emit1(0x60)   // iadd
                     .emit1(0xAC)   // ireturn
                     .build();
    JvmLowerer lowerer;
    auto r = lowerer.lower(bytes, 0, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    Graph& g = *r;
    PassContext ctx;
    ConstantFoldingPass pass;
    ASSERT_TRUE(pass.run(g, ctx).has_value());
    bool found_folded_add = false;
    for (std::size_t i = 0; i < g.size(); ++i) {
        NodeId id{static_cast<uint32_t>(i + 1)};
        if (g.node(id).kind == NodeKind::Add && g.node(id).is_const()) {
            EXPECT_EQ(g.side(id).const_value.i64, 10);
            found_folded_add = true;
        }
    }
    EXPECT_TRUE(found_folded_add);
}

TEST(JvmLowererTest, WideIloadDecodesAndLowersCorrectly) {
    // wide + iload + u2 5 = 0xC4 0x15 0x00 0x05 (big-endian)
    // Then ireturn.
    std::vector<uint8_t> raw = {0xC4, 0x15, 0x00, 0x05, 0xAC};
    JvmLowerer lowerer;
    auto r = lowerer.lower(raw, 6, 0);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // The local index 5 should be accessed.
    EXPECT_GT(r->size(), 1u);
}
