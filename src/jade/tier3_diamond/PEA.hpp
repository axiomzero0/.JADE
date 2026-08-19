// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/PEA.hpp
//
// Partial Escape Analysis (Stadler et al., 2013).
//
// Unlike binary Escape Analysis, PEA delays allocation to the EXACT control-
// flow path where the object escapes. In non-escaping paths, the object's
// fields remain as independent SSA scalar values, enabling register allocation.
//
// Algorithm:
//   1. For each Allocate/NewObj/NewArr/Box node, compute per-block escape
//      state: { NoEscape, Escape, Materialized }.
//   2. If state is uniformly NoEscape: scalar-replace (delegate to SRA).
//   3. If state has partial escape: split the allocation — insert a
//      Materialize node at each escaping block, and a Phi at the merge.
//
// For the initial milestone, we implement the analysis + simple elimination
// (no materialization splitting yet). Materialization requires block structure.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade::tier3 {

class PEAPass final : public jade::Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "PEA"; }
    [[nodiscard]] jade::Result<void> run(jade::Graph& g, jade::PassContext& ctx) override;
};

}  // namespace jade::tier3
