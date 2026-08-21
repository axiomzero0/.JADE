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
    // Note: Throw is declared below in the exception-handling section.

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

    // ─────────────────────────────────────────────────────────────────────
    // C# / CIL-specific node kinds (see docs/08-csharp-target.md)
    // ─────────────────────────────────────────────────────────────────────

    // ── Boxing and type conversions ──────────────────────────────────────
    Box,            // box a value type into a heap object
    Unbox,          // unbox to a managed pointer to the value-type storage
    UnboxAny,       // unbox and copy the value out
    IsInst,         // C# `is` — null on mismatch, no throw
    CastClass,      // C# `(T)x` — throws InvalidCastException on mismatch
    LdNull,         // push null reference
    LdStr,          // load a string literal from metadata

    // ── Conversions ──────────────────────────────────────────────────────
    ConvI1, ConvI2, ConvI4, ConvI8,    // signed
    ConvU1, ConvU2, ConvU4, ConvU8,    // unsigned
    ConvR4, ConvR8,                    // floating-point
    ConvI, ConvU,                      // native int/uint
    ConvOvfI1, ConvOvfI2, ConvOvfI4, ConvOvfI8,
    ConvOvfU1, ConvOvfU2, ConvOvfU4, ConvOvfU8,

    // ── Field and array access (C#) ──────────────────────────────────────
    LdFld,          // ldfld
    StFld,          // stfld
    LdFlda,         // ldflda — managed pointer to a field
    LdElem,         // ldelem.*
    StElem,         // stelem.*
    LdElemA,        // ldelema
    NewArr,         // newarr

    // ── Object operations ────────────────────────────────────────────────
    NewObj,          // newobj — allocates and constructs
    CallVirt,        // callvirt — virtual dispatch
    Constrained,     // constrained. prefix for callvirt on a value type

    // ── Locals and arguments (CIL) ───────────────────────────────────────
    LdArg,           // ldarg.*
    StArg,           // starg
    LdLoc,           // ldloc.*
    StLoc,           // stloc.*
    LdArga,          // ldarga
    LdLoca,          // ldloca

    // ── Exception handling ──────────────────────────────────────────────
    Throw,           // throw
    Rethrow,         // rethrow
    Leave,           // leave — jump out of try, runs finally
    EndFinally,      // endfinally

    // ─────────────────────────────────────────────────────────────────────
    // Java / JVM-specific node kinds (see docs/09-java-target.md)
    // ─────────────────────────────────────────────────────────────────────

    // ── Monitor operations (synchronized blocks) ─────────────────────────
    MonitorEnter,    // monitorenter — acquire object monitor
    MonitorExit,     // monitorexit  — release object monitor

    // ── Invokedynamic (lambdas, string concat) ──────────────────────────
    InvokeDynamic,   // invokedynamic — resolves through a bootstrap method

    // ── PEA materialization (DIAMOND tier) ──────────────────────────────
    Materialize,     // PEA: heap-allocate a scalar-replaced object at the
                     // exact point where it escapes. Inputs: field values.
                     // Output: the materialized object reference.

    // ── SLP vectorization (DIAMOND tier) ────────────────────────────────
    VectorOp,        // SLP: a pack of 2/4/8 isomorphic independent scalar ops
                     // replaced by a single SIMD vector op. Inputs: the
                     // scalar values to pack. side_data::vector_kind records
                     // the original NodeKind (Add/Sub/Mul/etc.). The emitter
                     // lowers this to paddd/psubd/pmulld/pandd/pord/pxord.
    VectorExtract,   // SLP: extract lane N from a VectorOp. Input: the
                     // VectorOp. side_data::vector_lane records the lane index.

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
