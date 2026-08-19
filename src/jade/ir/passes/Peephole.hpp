// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/Peephole.hpp
//
// Post-register-allocation peephole optimization (Standard catalog §10).
//
// Runs on the linear instruction stream after regalloc, immediately before
// asmjit emits bytes. Combines redundant moves, folds immediates, fuses
// test-and-branch.
//
// For the initial milestone, the peephole pass runs on the SoN IR (before
// regalloc) and performs algebraic peephole transforms:
//   - x + 0 → x  (already handled by AlgebraicSimplification)
//   - x * 2^k → x << k  (strength reduction)
//   - (x << k) >> k → x & mask  (mask extraction)

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class PeepholePass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "Peephole"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
