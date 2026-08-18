// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/NodeKind.hpp
//
// NodeKind is a flat enum (Rule B.3):
//   "No RTTI in the JIT hot path."
//   Passes dispatch via switch statements.

#pragma once

#include <cstdint>
#include <string_view>

namespace jade {

// ─────────────────────────────────────────────────────────────────────────────
// NodeKind — discriminates all node types in the Sea of Nodes IR.
// Underlying type is uint8_t — keeps sizeof(Node) small (Rule 3 of SoN).
// ─────────────────────────────────────────────────────────────────────────────
enum class NodeKind : uint8_t {
    // ── Sentinel ─────────────────────────────────────────────────────────
    Invalid = 0,

    // ── Control flow ─────────────────────────────────────────────────────
    Start,          // graph entry; single per function
    Region,         // merge point with ≥2 predecessors
    Loop,           // back-edge merge point
    If,             // conditional branch; produces IfTrue/IfFalse
    IfTrue,         // takes If, projects true-successor
    IfFalse,        // takes If, projects false-successor
    Switch,         // multi-way branch
    Jump,           // unconditional goto
    Return,         // returns a value, exits function
    Throw,          // throws an exception

    // ── Constants ────────────────────────────────────────────────────────
    ConstInt,       // 64-bit signed integer
    ConstFloat,     // 64-bit IEEE-754 double
    ConstBool,      // 1-bit boolean
    ConstNull,      // .JADE null
    ConstString,    // interned string ID

    // ── Arithmetic (pure) ────────────────────────────────────────────────
    Add, Sub, Mul, Div, Mod, Neg,
    And, Or, Xor, Not, Shl, Shr, Sar,
    Eq, Ne, Lt, Gt, Lte, Gte,

    // ── Type operations ──────────────────────────────────────────────────
    CheckInt,       // guard: value is Int; deopt if not
    CheckNotNull,   // guard: value is non-null; deopt if null
    CheckShape,     // guard: value has expected Shape; deopt if not
    CheckBounds,    // guard: 0 <= idx < length; deopt if not
    CheckClass,     // guard: value's class == expected; deopt if not
    ToFloat,        // int -> float
    ToInt,          // float -> int (truncating)
    ToBool,         // truthiness
    IsInt,
    IsFloat,
    IsNull,

    // ── Memory ops ───────────────────────────────────────────────────────
    Allocate,       // allocate a new object of given Shape
    LoadField,      // obj, field_id -> value
    StoreField,     // obj, field_id, value -> void
    LoadElement,    // array, idx -> value
    StoreElement,   // array, idx, value -> void
    ArrayLength,    // array -> int

    // ── Calls ────────────────────────────────────────────────────────────
    Call,           // generic call (virtual / dynamic dispatch)
    CallKnown,      // direct call to a known function (post-CHA / PEA)
    TailCall,       // tail call — reuse frame

    // ── SSA plumbing ─────────────────────────────────────────────────────
    Phi,            // merge of values at a Region/Loop
    Copy,           // identity (used by regalloc)

    // ── Misc / runtime ────────────────────────────────────────────────────
    FrameState,     // snapshot of bytecode state for deopt reconstruction
    Safepoint,      // safepoint poll location
    Deopt,          // deoptimization trigger (cold path)
    Unreachable,    // unreachable after this point

    kCount,         // sentinel = number of node kinds
};

// Compile-time array of node-kind names for symbolic printing (Rule 51).
// Indexed by NodeKind.
struct NodeKindInfo {
    std::string_view name;
    bool is_pure;
    bool is_effect;     // participates in effect chain
    bool is_control;    // participates in control flow
    bool is_commutative;
    bool is_associative;
    bool is_guard;      // requires FrameState
    uint8_t num_data_inputs;  // expected fixed arity; 0xFF = variadic
};

// Defined in NodeKind.cpp (compiled; not header-only to keep binary small).
[[nodiscard]] const NodeKindInfo& node_kind_info(NodeKind k) noexcept;
[[nodiscard]] std::string_view node_kind_name(NodeKind k) noexcept;

}  // namespace jade
