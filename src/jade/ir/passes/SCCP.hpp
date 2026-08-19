// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/SCCP.hpp
//
// Sparse Conditional Constant Propagation (Wegman & Zadeck, 1991).
//
// Integrates constant propagation with abstract interpretation. Uses a
// worklist algorithm over the SSA graph. Lattice elements:
//   ⊤ (Top)    — unknown / not yet analyzed
//   c          — a specific constant value
//   ⊥ (Bottom) — unreachable / overdefined (multiple values)
//
// When a value is proven constant, downstream uses are rewritten to
// reference the constant directly. This often unlocks further folding
// in ConstantFolding.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class SCCPPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "SCCP"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
