// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/AlgebraicSimplification.hpp
//
// Standard catalog §1.2 (Algebraic Identity Elimination) and §1.3 (Annihilation).
//
//   x + 0 → x          x - 0 → x
//   x * 1 → x          x / 1 → x
//   x * 0 → 0          x & 0 → 0
//   x | 0 → x          x ^ 0 → x
//   x & x → x          x | x → x
//   !!x → x            x << 0 → x
//
// These passes run between other passes to expose more folding opportunities.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class AlgebraicSimplificationPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "AlgebraicSimplification"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
