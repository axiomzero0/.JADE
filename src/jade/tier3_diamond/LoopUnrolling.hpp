// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/LoopUnrolling.hpp
//
// Loop unrolling with cost model (Rule 47).
//
// Unroll hot loops by 2× or 4× to reduce branch overhead and expose ILP.
// Only runs if profile shows high iteration count and the loop body is small.
// Budget: max 8× unroll.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade::tier3 {

class LoopUnrollingPass final : public jade::Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "LoopUnrolling"; }
    [[nodiscard]] jade::Result<void> run(jade::Graph& g, jade::PassContext& ctx) override;
};

}  // namespace jade::tier3
