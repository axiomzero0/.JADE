// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/TypeId.hpp
//
// Type lattice element for type propagation (SCCP, type narrowing, devirt).

#pragma once

#include <cstdint>
#include <string_view>

namespace jade {

enum class TypeId : uint16_t {
    Bottom  = 0,   // ⊥ — unreachable
    Top     = 1,   // ⊤ — unknown
    Int     = 2,
    Float   = 3,
    Bool    = 4,
    Null    = 5,
    Object  = 6,
    Array   = 7,
    String  = 8,
    Function = 9,
    BigInt  = 10,  // promoted from Int on overflow
    // ... extend as needed

    kCount,
};

[[nodiscard]] std::string_view type_id_name(TypeId t) noexcept;

// Lattice operations.
[[nodiscard]] TypeId type_meet(TypeId a, TypeId b) noexcept;
[[nodiscard]] TypeId type_join(TypeId a, TypeId b) noexcept;

}  // namespace jade
