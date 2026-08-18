// SPDX-License-Identifier: MIT
// .JADE Compiler — core/Flags.hpp
//
// Type-safe bitmask for orthogonal boolean state (Rule 51):
//   "All orthogonal boolean state must be bitmasked."
//
//   Raw integers are forbidden for flag-like state. All bitmask types must have
//   symbolic printing, debugger visualizers, and compile-time validation.
//
// Usage:
//   enum class NodeFlag : uint16_t { Pure = 1<<0, Effect = 1<<1, ... };
//   using NodeFlags = Flags<NodeFlag>;
//
//   NodeFlags f = NodeFlag::Pure | NodeFlag::Effect;
//   if (f.has(NodeFlag::Pure)) { ... }
//   f |= NodeFlag::HasState;
//   std::print("{}", f);   // "Pure|Effect|HasState"

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <span>
#include <ostream>
#include <array>

namespace jade {

// ─────────────────────────────────────────────────────────────────────────────
// Flags<E> — type-safe bitmask wrapper for an enum class E.
// E must be an enum class with underlying type uint8_t/uint16_t/uint32_t.
// ─────────────────────────────────────────────────────────────────────────────
template <typename E>
class Flags {
public:
    using enum_type = E;
    using value_type = E;
    using underlying_type = std::underlying_type_t<E>;

    static_assert(std::is_enum_v<E>,
                  "Flags<E> requires E to be an enum class");

    constexpr Flags() noexcept = default;
    constexpr Flags(E single) noexcept : bits_(static_cast<underlying_type>(single)) {}

    // ── Compound assignment ──────────────────────────────────────────────
    constexpr Flags& operator|=(Flags other) noexcept {
        bits_ |= other.bits_;
        return *this;
    }
    constexpr Flags& operator&=(Flags other) noexcept {
        bits_ &= other.bits_;
        return *this;
    }
    constexpr Flags& operator^=(Flags other) noexcept {
        bits_ ^= other.bits_;
        return *this;
    }

    // ── Bitwise operators (free functions, return Flags) ──────────────────
    friend constexpr Flags operator|(Flags a, Flags b) noexcept {
        return Flags{static_cast<underlying_type>(a.bits_ | b.bits_)};
    }
    friend constexpr Flags operator&(Flags a, Flags b) noexcept {
        return Flags{static_cast<underlying_type>(a.bits_ & b.bits_)};
    }
    friend constexpr Flags operator^(Flags a, Flags b) noexcept {
        return Flags{static_cast<underlying_type>(a.bits_ ^ b.bits_)};
    }
    friend constexpr Flags operator~(Flags a) noexcept {
        return Flags{static_cast<underlying_type>(~a.bits_)};
    }

    // Allow `Flags | E` and `E | Flags`.
    friend constexpr Flags operator|(Flags a, E b) noexcept { return a | Flags{b}; }
    friend constexpr Flags operator|(E a, Flags b) noexcept { return Flags{a} | b; }
    friend constexpr Flags operator&(Flags a, E b) noexcept { return a & Flags{b}; }
    friend constexpr Flags operator&(E a, Flags b) noexcept { return Flags{a} & b; }

    // ── Queries ──────────────────────────────────────────────────────────
    [[nodiscard]] constexpr bool has(E flag) const noexcept {
        return (bits_ & static_cast<underlying_type>(flag)) != 0;
    }
    [[nodiscard]] constexpr bool has_any_of(Flags mask) const noexcept {
        return (bits_ & mask.bits_) != 0;
    }
    [[nodiscard]] constexpr bool has_all_of(Flags mask) const noexcept {
        return (bits_ & mask.bits_) == mask.bits_;
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return bits_ == 0; }
    [[nodiscard]] constexpr underlying_type raw() const noexcept { return bits_; }

    // ── Comparisons ───────────────────────────────────────────────────────
    [[nodiscard]] constexpr bool operator==(const Flags&) const noexcept = default;

    // ── Mutators ──────────────────────────────────────────────────────────
    constexpr void set(E flag) noexcept { bits_ |= static_cast<underlying_type>(flag); }
    constexpr void clear(E flag) noexcept { bits_ &= ~static_cast<underlying_type>(flag); }
    constexpr void toggle(E flag) noexcept { bits_ ^= static_cast<underlying_type>(flag); }

    // ── To-string (slow path; not in hot loops) ───────────────────────────
    // Symbolic printing is mandatory per Rule 51. Implementations provide
    // `flag_names(E)` in the same namespace as the enum.
    [[nodiscard]] std::string to_string() const;

private:
    explicit constexpr Flags(underlying_type raw) noexcept : bits_(raw) {}
    underlying_type bits_{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// flag_names<E>() — provided by the user in the enum's namespace.
// Returns a span of (bit, name) pairs for symbolic printing.
// ─────────────────────────────────────────────────────────────────────────────
template <typename E>
struct FlagNameEntry {
    E bit;
    std::string_view name;
};

template <typename E>
[[nodiscard]] std::string to_string(Flags<E> flags);

}  // namespace jade

// ─────────────────────────────────────────────────────────────────────────────
// Implementation of to_string (templates instantiated on demand).
// ─────────────────────────────────────────────────────────────────────────────
namespace jade {

template <typename E>
[[nodiscard]] consteval std::size_t flag_count() {
    // Count set bits in the OR of all named flags — but to keep things simple
    // and consteval-friendly we just rely on the runtime table provided by the
    // user. The flag_names<E>() function returns a span; we walk it.
    return 0;  // unused; kept for symmetry
}

template <typename E>
[[nodiscard]] std::string Flags<E>::to_string() const {
    return jade::to_string(*this);
}

// ─────────────────────────────────────────────────────────────────────────────
// Free operator|(E, E) — produces Flags<E>. This is what makes expressions like
// `NodeFlag::Pure | NodeFlag::Effect` work without an explicit Flags{} wrap.
// ─────────────────────────────────────────────────────────────────────────────
template <typename E>
[[nodiscard]] constexpr Flags<E> operator|(E a, E b) noexcept {
    return Flags<E>{a} | Flags<E>{b};
}

}  // namespace jade

// ─────────────────────────────────────────────────────────────────────────────
// Pretty-printer for std::ostream. Format: "Flag1|Flag2" or "(none)".
// ─────────────────────────────────────────────────────────────────────────────
template <typename E>
inline std::ostream& operator<<(std::ostream& os, jade::Flags<E> flags) {
    return os << flags.to_string();
}
