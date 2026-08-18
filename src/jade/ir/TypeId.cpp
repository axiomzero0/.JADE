// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/TypeId.cpp

#include "jade/ir/TypeId.hpp"
#include <array>
#include <string_view>

namespace jade {

namespace {

constexpr std::string_view kNames[] = {
    "Bottom", "Top", "Int", "Float", "Bool", "Null",
    "Object", "Array", "String", "Function", "BigInt",
};

static_assert(std::size(kNames) == static_cast<std::size_t>(TypeId::kCount),
              "TypeId name table out of sync");

}  // namespace

std::string_view type_id_name(TypeId t) noexcept {
    const auto idx = static_cast<std::size_t>(t);
    if (idx < std::size(kNames)) return kNames[idx];
    return "Unknown";
}

// Meet (greatest lower bound): the most-specific type compatible with both.
//   meet(Top, x)   = x
//   meet(Bottom,x) = Bottom
//   meet(x, x)     = x
//   meet(x, y)     = Bottom   (no common subtype; treated conservatively)
TypeId type_meet(TypeId a, TypeId b) noexcept {
    if (a == TypeId::Top)    return b;
    if (b == TypeId::Top)    return a;
    if (a == TypeId::Bottom) return TypeId::Bottom;
    if (b == TypeId::Bottom) return TypeId::Bottom;
    if (a == b) return a;
    return TypeId::Bottom;
}

// Join (least upper bound): the most-general type that includes both.
TypeId type_join(TypeId a, TypeId b) noexcept {
    if (a == TypeId::Bottom) return b;
    if (b == TypeId::Bottom) return a;
    if (a == b) return a;
    if (a == TypeId::Top || b == TypeId::Top) return TypeId::Top;
    // Int + Float -> Float
    if ((a == TypeId::Int && b == TypeId::Float) ||
        (a == TypeId::Float && b == TypeId::Int)) {
        return TypeId::Float;
    }
    return TypeId::Object;
}

}  // namespace jade
