// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/TypeNarrowing.hpp
//
// Type narrowing (Standard catalog §7.1).
//
// If profile says a value is always Int, insert a CheckInt guard at the
// definition, then propagate TypeId::Int through the graph. Downstream
// Add nodes become integer-only (no float promotion check).

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class TypeNarrowingPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "TypeNarrowing"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
