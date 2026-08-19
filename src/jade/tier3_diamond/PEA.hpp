// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/PEA.hpp
//
// Full Partial Escape Analysis (Stadler et al., 2013).
//
// The crown jewel of .JADE's optimizer. Unlike binary Escape Analysis,
// PEA delays allocation to the EXACT control-flow path where the object
// escapes. In non-escaping paths, the object's fields remain as
// independent SSA scalar values, enabling register allocation.
//
// Algorithm:
//   Phase 1: Collect all allocation nodes (Allocate/NewObj/NewArr/Box).
//   Phase 2: For each allocation, compute per-block escape state using
//            BuildRegions BlockStructure:
//              - NoEscape: the object is never used in an escaping way
//                in this block or any successor.
//              - Escape: the object escapes in this block.
//              - Materialized: the object has been materialized (allocated
//                on the heap) at or before this block.
//   Phase 3: For allocations with uniform NoEscape:
//              - Scalar-replace: eliminate the Allocate, replace
//                LoadField/StoreField with direct SSA edges.
//   Phase 4: For allocations with partial escape (NoEscape on some paths,
//            Escape on others):
//              - Insert a Materialize node at each escaping block.
//              - Insert Phi nodes at merge points for each scalar field.
//              - The Materialize node allocates the object on the heap
//                and writes the scalar fields into it.
//   Phase 5: Eliminate the original Allocate node.
//   Phase 6: Run DCE to clean up dead stores/loads.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade::tier3 {

class PEAPass final : public jade::Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "PEA"; }
    [[nodiscard]] jade::Result<void> run(jade::Graph& g, jade::PassContext& ctx) override;
};

}  // namespace jade::tier3
