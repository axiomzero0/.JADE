// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/NodeKind.cpp
//
// Metadata table for every NodeKind. Indexed at runtime via node_kind_info().

#include "jade/ir/NodeKind.hpp"

namespace jade {

namespace {

// Indexed by NodeKind. MUST stay in sync with the enum.
// Order: pure? effect? control? commutative? associative? guard? num_data_inputs
constexpr NodeKindInfo kTable[] = {
    // Invalid
    {"Invalid",       false, false, false, false, false, false, 0},

    // ── Control flow ──
    {"Start",         false, true,  true,  false, false, false, 0},
    {"Region",        false, false, true,  false, false, false, 0xFF},  // variadic
    {"Loop",          false, false, true,  false, false, false, 0xFF},  // variadic
    {"If",            false, false, true,  false, false, false, 1},     // cond
    {"IfTrue",        false, false, true,  false, false, false, 0},
    {"IfFalse",       false, false, true,  false, false, false, 0},
    {"Switch",        false, false, true,  false, false, false, 1},     // value
    {"Jump",          false, false, true,  false, false, false, 0},
    {"Return",        false, true,  true,  false, false, false, 1},     // value
    // Throw moved to the exception-handling section below.

    // ── Constants ──
    {"ConstInt",      true,  false, false, false, false, false, 0},
    {"ConstFloat",    true,  false, false, false, false, false, 0},
    {"ConstBool",     true,  false, false, false, false, false, 0},
    {"ConstNull",     true,  false, false, false, false, false, 0},
    {"ConstString",   true,  false, false, false, false, false, 0},

    // ── Arithmetic ──
    {"Add",           true,  false, false, true,  true,  false, 2},
    {"Sub",           true,  false, false, false, false, false, 2},
    {"Mul",           true,  false, false, true,  true,  false, 2},
    {"Div",           true,  false, false, false, false, false, 2},
    {"Mod",           true,  false, false, false, false, false, 2},
    {"Neg",           true,  false, false, false, false, false, 1},
    {"And",           true,  false, false, true,  true,  false, 2},
    {"Or",            true,  false, false, true,  true,  false, 2},
    {"Xor",           true,  false, false, true,  false, false, 2},
    {"Not",           true,  false, false, false, false, false, 1},
    {"Shl",           true,  false, false, false, false, false, 2},
    {"Shr",           true,  false, false, false, false, false, 2},     // logical
    {"Sar",           true,  false, false, false, false, false, 2},     // arithmetic
    {"Eq",            true,  false, false, true,  false, false, 2},
    {"Ne",            true,  false, false, true,  false, false, 2},
    {"Lt",            true,  false, false, false, false, false, 2},
    {"Gt",            true,  false, false, false, false, false, 2},
    {"Lte",           true,  false, false, false, false, false, 2},
    {"Gte",           true,  false, false, false, false, false, 2},

    // ── Type ops ── (guards)
    {"CheckInt",      false, false, true,  false, false, true,  1},
    {"CheckNotNull",  false, false, true,  false, false, true,  1},
    {"CheckShape",    false, false, true,  false, false, true,  1},
    {"CheckBounds",   false, false, true,  false, false, true,  2},     // idx, len
    {"CheckClass",    false, false, true,  false, false, true,  1},
    {"ToFloat",       true,  false, false, false, false, false, 1},
    {"ToInt",         true,  false, false, false, false, false, 1},
    {"ToBool",        true,  false, false, false, false, false, 1},
    {"IsInt",         true,  false, false, false, false, false, 1},
    {"IsFloat",       true,  false, false, false, false, false, 1},
    {"IsNull",        true,  false, false, false, false, false, 1},

    // ── Memory ops ──
    {"Allocate",      false, true,  false, false, false, false, 0xFF},  // shape args
    {"LoadField",     false, true,  false, false, false, false, 1},     // obj
    {"StoreField",    false, true,  false, false, false, false, 2},     // obj, value
    {"LoadElement",   false, true,  false, false, false, false, 2},     // array, idx
    {"StoreElement",  false, true,  false, false, false, false, 3},     // array, idx, value
    {"ArrayLength",   false, true,  false, false, false, false, 1},

    // ── Calls ──
    {"Call",          false, true,  false, false, false, false, 0xFF},  // callee, args...
    {"CallKnown",     false, true,  false, false, false, false, 0xFF},
    {"TailCall",       false, true,  true,  false, false, false, 0xFF},

    // ── SSA plumbing ──
    {"Phi",           true,  false, false, false, false, false, 0xFF},  // variadic
    {"Copy",          true,  false, false, false, false, false, 1},

    // ── Misc ──
    {"FrameState",    false, false, false, false, false, false, 0xFF},
    {"Safepoint",     false, true,  false, false, false, false, 0},
    {"Deopt",         false, true,  true,  false, false, false, 0},
    {"Unreachable",   false, false, true,  false, false, false, 0},

    // ─────────────────────────────────────────────────────────────────────
    // C# / CIL-specific node kinds (see docs/08-csharp-target.md)
    // ─────────────────────────────────────────────────────────────────────

    // ── Boxing and type conversions ──
    {"Box",           false, true,  false, false, false, false, 1},     // value
    {"Unbox",         false, true,  false, false, false, false, 1},     // obj
    {"UnboxAny",      false, true,  false, false, false, false, 1},     // obj
    {"IsInst",        false, true,  false, false, false, false, 1},     // obj
    {"CastClass",     false, true,  false, false, false, false, 1},     // obj
    {"LdNull",        true,  false, false, false, false, false, 0},
    {"LdStr",         true,  false, false, false, false, false, 0},

    // ── Conversions (pure) ──
    {"ConvI1",        true,  false, false, false, false, false, 1},
    {"ConvI2",        true,  false, false, false, false, false, 1},
    {"ConvI4",        true,  false, false, false, false, false, 1},
    {"ConvI8",        true,  false, false, false, false, false, 1},
    {"ConvU1",        true,  false, false, false, false, false, 1},
    {"ConvU2",        true,  false, false, false, false, false, 1},
    {"ConvU4",        true,  false, false, false, false, false, 1},
    {"ConvU8",        true,  false, false, false, false, false, 1},
    {"ConvR4",        true,  false, false, false, false, false, 1},
    {"ConvR8",        true,  false, false, false, false, false, 1},
    {"ConvI",         true,  false, false, false, false, false, 1},
    {"ConvU",         true,  false, false, false, false, false, 1},
    {"ConvOvfI1",     false, true,  false, false, false, true,  1},      // can throw on overflow
    {"ConvOvfI2",     false, true,  false, false, false, true,  1},
    {"ConvOvfI4",     false, true,  false, false, false, true,  1},
    {"ConvOvfI8",     false, true,  false, false, false, true,  1},
    {"ConvOvfU1",     false, true,  false, false, false, true,  1},
    {"ConvOvfU2",     false, true,  false, false, false, true,  1},
    {"ConvOvfU4",     false, true,  false, false, false, true,  1},
    {"ConvOvfU8",     false, true,  false, false, false, true,  1},

    // ── Field and array access (C#) ──
    {"LdFld",         false, true,  false, false, false, false, 1},     // obj
    {"StFld",         false, true,  false, false, false, false, 2},     // obj, value
    {"LdFlda",        false, true,  false, false, false, false, 1},     // obj
    {"LdElem",        false, true,  false, false, false, false, 2},     // array, idx
    {"StElem",        false, true,  false, false, false, false, 3},     // array, idx, value
    {"LdElemA",       false, true,  false, false, false, false, 2},     // array, idx
    {"NewArr",        false, true,  false, false, false, false, 1},     // length

    // ── Object operations ──
    {"NewObj",        false, true,  false, false, false, false, 0xFF},  // args...
    {"CallVirt",      false, true,  false, false, false, false, 0xFF},  // this, args...
    {"Constrained",   false, true,  false, false, false, false, 0},     // prefix; the
                                                                                // callvirt follows

    // ── Locals and arguments ──
    {"LdArg",         true,  false, false, false, false, false, 0},
    {"StArg",         false, true,  false, false, false, false, 1},
    {"LdLoc",         true,  false, false, false, false, false, 0},
    {"StLoc",         false, true,  false, false, false, false, 1},
    {"LdArga",        true,  false, false, false, false, false, 0},
    {"LdLoca",        true,  false, false, false, false, false, 0},

    // ── Exception handling ──
    {"Throw",         false, true,  true,  false, false, false, 1},
    {"Rethrow",       false, true,  true,  false, false, false, 0},
    {"Leave",         false, true,  true,  false, false, false, 0},
    {"EndFinally",    false, true,  true,  false, false, false, 0},

    // ─────────────────────────────────────────────────────────────────────
    // Java / JVM-specific node kinds (see docs/09-java-target.md)
    // ─────────────────────────────────────────────────────────────────────

    // ── Monitor operations ──
    {"MonitorEnter",  false, true,  false, false, false, false, 1},   // obj
    {"MonitorExit",   false, true,  false, false, false, false, 1},   // obj

    // ── Invokedynamic ──
    {"InvokeDynamic", false, true,  false, false, false, false, 0xFF}, // args...
};

static_assert(std::size(kTable) == static_cast<std::size_t>(NodeKind::kCount),
              "NodeKindInfo table is out of sync with NodeKind enum");

}  // namespace

const NodeKindInfo& node_kind_info(NodeKind k) noexcept {
    const auto idx = static_cast<std::size_t>(k);
    if (idx < std::size(kTable)) {
        return kTable[idx];
    }
    // Fallback to Invalid entry rather than crashing.
    return kTable[0];
}

std::string_view node_kind_name(NodeKind k) noexcept {
    return node_kind_info(k).name;
}

}  // namespace jade
