// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/Value.cpp

#include "jade/tier0_granit/Value.hpp"
#include <bit>
#include <format>
#include <limits>
#include <stdexcept>

namespace jade::granit {

// ─────────────────────────────────────────────────────────────────────────────
// Truthiness — C# rules.
//   - bool:        the bool itself
//   - integer:    != 0
//   - float:       != 0.0 (and not NaN; C# truthiness on float requires != 0)
//   - object ref:  != null
//   - managed ptr: never null (always points somewhere)
//   - native ptr:  != null
// ─────────────────────────────────────────────────────────────────────────────
bool truthy(const Value& v) {
    if (std::holds_alternative<std::monostate>(v)) return false;
    if (std::holds_alternative<int32_t>(v))   return std::get<int32_t>(v) != 0;
    if (std::holds_alternative<int64_t>(v))   return std::get<int64_t>(v) != 0;
    if (std::holds_alternative<float>(v))     return std::get<float>(v) != 0.0f;
    if (std::holds_alternative<double>(v))    return std::get<double>(v) != 0.0;
    if (std::holds_alternative<ObjectHandle>(v)) return !std::get<ObjectHandle>(v).is_null();
    if (std::holds_alternative<ManagedPointer>(v)) return !std::get<ManagedPointer>(v).is_null();
    if (std::holds_alternative<NativePointer>(v))  return !std::get<NativePointer>(v).is_null();
    return false;
}

bool value_equals(const Value& a, const Value& b) {
    // Per ECMA-335: comparison is type-specific. Mixing types is invalid.
    if (a.index() != b.index()) return false;
    return a == b;
}

std::string to_string(const Value& v) {
    if (std::holds_alternative<std::monostate>(v)) return "uninit";
    if (std::holds_alternative<int32_t>(v))   return std::format("int32:{}", std::get<int32_t>(v));
    if (std::holds_alternative<int64_t>(v))   return std::format("int64:{}", std::get<int64_t>(v));
    if (std::holds_alternative<float>(v))     return std::format("float32:{}", std::get<float>(v));
    if (std::holds_alternative<double>(v))    return std::format("float64:{}", std::get<double>(v));
    if (std::holds_alternative<ObjectHandle>(v)) {
        const auto& h = std::get<ObjectHandle>(v);
        return h.is_null() ? "null" : std::format("obj:#{}@{}", h.index(), h.generation());
    }
    if (std::holds_alternative<ManagedPointer>(v)) {
        const auto& p = std::get<ManagedPointer>(v);
        return std::format("mptr:({}+{})", p.base.value, p.offset);
    }
    if (std::holds_alternative<NativePointer>(v)) {
        return std::format("nptr:0x{:x}", std::get<NativePointer>(v).value);
    }
    return "unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// Integer arithmetic helpers.
// ─────────────────────────────────────────────────────────────────────────────
int32_t wrap_add_i32(int32_t a, int32_t b) noexcept {
    return std::bit_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}
int32_t wrap_sub_i32(int32_t a, int32_t b) noexcept {
    return std::bit_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}
int32_t wrap_mul_i32(int32_t a, int32_t b) noexcept {
    return std::bit_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}
int64_t wrap_add_i64(int64_t a, int64_t b) noexcept {
    return std::bit_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}
int64_t wrap_sub_i64(int64_t a, int64_t b) noexcept {
    return std::bit_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}
int64_t wrap_mul_i64(int64_t a, int64_t b) noexcept {
    return std::bit_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

// Checked arithmetic — returns false on overflow (caller decides whether to throw).
bool checked_add_i32(int32_t a, int32_t b, int32_t& out) noexcept {
    const int64_t r = static_cast<int64_t>(a) + static_cast<int64_t>(b);
    if (r < std::numeric_limits<int32_t>::min() ||
        r > std::numeric_limits<int32_t>::max()) return false;
    out = static_cast<int32_t>(r);
    return true;
}
bool checked_sub_i32(int32_t a, int32_t b, int32_t& out) noexcept {
    const int64_t r = static_cast<int64_t>(a) - static_cast<int64_t>(b);
    if (r < std::numeric_limits<int32_t>::min() ||
        r > std::numeric_limits<int32_t>::max()) return false;
    out = static_cast<int32_t>(r);
    return true;
}
bool checked_mul_i32(int32_t a, int32_t b, int32_t& out) noexcept {
    const int64_t r = static_cast<int64_t>(a) * static_cast<int64_t>(b);
    if (r < std::numeric_limits<int32_t>::min() ||
        r > std::numeric_limits<int32_t>::max()) return false;
    out = static_cast<int32_t>(r);
    return true;
}

}  // namespace jade::granit
