// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/Value.cpp

#include "jade/tier0_granit/Value.hpp"
#include <bit>
#include <format>
#include <limits>

namespace jade::granit {

// ─────────────────────────────────────────────────────────────────────────────
// Pretty-printing (debug only — not on the hot path).
// ─────────────────────────────────────────────────────────────────────────────
std::string to_string(const Value& v) {
    switch (v.tag()) {
        case ValueTag::Uninit:     return "uninit";
        case ValueTag::Int32:      return std::format("int32:{}", v.as_int32());
        case ValueTag::Int64:      return std::format("int64:{}", v.as_int64());
        case ValueTag::Float:      return std::format("float:{}", v.as_float());
        case ValueTag::ObjectRef:  {
            auto h = v.as_object();
            return h.is_null() ? "null" : std::format("obj:#{}@{}", h.index(), h.generation());
        }
        case ValueTag::Null:       return "null";
        case ValueTag::ManagedPtr: return std::format("mptr:0x{:016x}", v.as_managed_ptr().packed);
        case ValueTag::NativePtr:  return std::format("nptr:0x{:016x}", v.as_native_ptr().value);
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
