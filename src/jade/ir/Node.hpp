// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/Node.hpp
//
// The Node value object (Rule 3 of SoN Design):
//   "Nodes are compact value objects."
//   Target sizeof(Node) ≈ 32 bytes.
//
//   No virtual functions. Kind dispatch is via switch (Rule B.3).

#pragma once

#include "jade/core/NodeId.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/TypeId.hpp"

#include <cstdint>

namespace jade {

// ─────────────────────────────────────────────────────────────────────────────
// Node — flat value object stored in a Graph-side vector.
// Layout is hand-tuned to keep sizeof(Node) close to 32 bytes.
// ─────────────────────────────────────────────────────────────────────────────
struct Node {
    NodeKind    kind{NodeKind::Invalid};   // 1 byte  — switch-dispatch key (B.3)
    NodeFlags   flags{};                    // 2 bytes — bitmask (Rule 51)
    uint8_t     arity_hint{0};              // 1 byte  — expected data input count
                                            //           (0xFF = variadic)
    uint8_t     _pad{0};                    // 1 byte  — explicit pad for layout
    TypeId      type{TypeId::Top};          // 2 bytes — type lattice element
    uint16_t    _pad2{0};                   // 2 bytes

    EdgeSlice   data_inputs;                // 8 bytes — slice into global EdgePool
    EdgeSlice   ctrl_input;                // 8 bytes — single control input (slice for uniformity)
    EdgeSlice   effect_input;               // 8 bytes — single effect input

    FrameStateId state;                    // 4 bytes — for guards (Rule A.3)

    // Total: 1+2+1+1+2+2+8+8+8+4 = 37 bytes; padded to 40.
    // We pack tightly with bitfields if needed in a future iteration; for now
    // the simplicity of explicit fields wins. Hot-path access is by offset.

    // ── Convenience queries ───────────────────────────────────────────────
    [[nodiscard]] constexpr bool is_pure() const noexcept {
        return flags.has(NodeFlag::Pure);
    }
    [[nodiscard]] constexpr bool is_effect() const noexcept {
        return flags.has(NodeFlag::Effect);
    }
    [[nodiscard]] constexpr bool is_control() const noexcept {
        return flags.has(NodeFlag::Control);
    }
    [[nodiscard]] constexpr bool is_guard() const noexcept {
        return flags.has(NodeFlag::IsGuard);
    }
    [[nodiscard]] constexpr bool is_const() const noexcept {
        return flags.has(NodeFlag::IsConst);
    }
    [[nodiscard]] constexpr bool is_dead() const noexcept {
        return flags.has(NodeFlag::IsDead);
    }
    [[nodiscard]] constexpr bool has_state() const noexcept {
        return flags.has(NodeFlag::HasState);
    }
};

static_assert(sizeof(Node) <= 40, "Node is too large — see docs/02-son-ir.md");

// ─────────────────────────────────────────────────────────────────────────────
// NodeSideData — side table for data that doesn't fit in Node itself.
// Stored parallel to the Node vector in Graph.
// ─────────────────────────────────────────────────────────────────────────────
struct NodeSideData {
    // Constant value (for ConstInt / ConstFloat / ConstBool).
    union ConstValue {
        int64_t     i64;
        double      f64;
        bool        b;
        uint32_t    str_id;
        ConstValue() : i64(0) {}
    };
    ConstValue const_value{};

    // For Allocate: the ShapeId.
    ShapeId shape_id{};

    // For LoadField/StoreField: the field offset (in bytes) and field id.
    uint16_t field_offset{0};
    StringId field_id{};

    // For CheckClass: the expected class id.
    uint32_t class_id{0};

    // Profile frequency: branch-taken ratio (0..1.0 fixed point in 0..65535).
    // Used by GCM and block reordering.
    uint16_t profile_freq{0};

    // Bytecode source position for deopt and debugging.
    uint32_t bc_offset{0xFFFF'FFFF};

    // For VectorOp: the original scalar NodeKind (Add/Sub/Mul/And/Or/Xor).
    // The emitter uses this to pick the right SIMD instruction (paddd/psubd/etc.).
    NodeKind vector_kind{NodeKind::Invalid};

    // For VectorOp: the number of lanes (2, 4, or 8).
    uint8_t vector_lanes{0};

    // For VectorExtract: the lane index to extract (0..vector_lanes-1).
    uint8_t vector_lane{0};
};

}  // namespace jade
