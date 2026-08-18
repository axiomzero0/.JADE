// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/NodeFlag.cpp

#include "jade/ir/NodeFlag.hpp"
#include <string>
#include <string_view>

namespace jade {

namespace {

struct Entry {
    NodeFlag bit;
    std::string_view name;
};

// Order matters for symbolic printing (this is the order shown).
constexpr Entry kEntries[] = {
    {NodeFlag::Pure,              "Pure"},
    {NodeFlag::Effect,            "Effect"},
    {NodeFlag::Control,           "Control"},
    {NodeFlag::Commutative,       "Commutative"},
    {NodeFlag::Associative,       "Associative"},
    {NodeFlag::NoThrow,           "NoThrow"},
    {NodeFlag::IsGuard,           "IsGuard"},
    {NodeFlag::HasState,          "HasState"},
    {NodeFlag::IsConst,           "IsConst"},
    {NodeFlag::IsDead,            "IsDead"},
    {NodeFlag::IsScheduled,       "IsScheduled"},
    {NodeFlag::HasSideExit,       "HasSideExit"},
    {NodeFlag::IsCold,            "IsCold"},
    {NodeFlag::IsLoop,            "IsLoop"},
    {NodeFlag::HasTypeNarrowing,  "HasTypeNarrowing"},
};

}  // namespace

std::string_view flag_bit_name(NodeFlag bit) {
    for (const auto& e : kEntries) {
        if (e.bit == bit) return e.name;
    }
    return "Unknown";
}

std::string to_string(NodeFlags flags) {
    if (flags.empty()) return "(none)";
    std::string out;
    bool first = true;
    for (const auto& e : kEntries) {
        if (flags.has(e.bit)) {
            if (!first) out += '|';
            out.append(e.name);
            first = false;
        }
    }
    if (first) return "(none)";
    return out;
}

}  // namespace jade

// ─────────────────────────────────────────────────────────────────────────────
// to_string specialization for Flags<NodeFlag> — required by Flags<E>::to_string.
// ─────────────────────────────────────────────────────────────────────────────
namespace jade {

template <>
std::string to_string<NodeFlag>(Flags<NodeFlag> flags) {
    return jade::to_string(static_cast<NodeFlags>(flags));
}

}  // namespace jade
