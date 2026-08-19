// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/BCE.hpp
//
// Bounds Check Elimination (Standard catalog §1.5).
//
// Uses affine range analysis on loop induction variables. If
// 0 <= i < length is proven at the loop header, dominate and eliminate
// all inner CheckBounds nodes for that array.
//
// For the initial milestone (no Loop regions), this pass conservatively
// eliminates CheckBounds nodes where the index is a ConstInt >= 0 and
// the length is also a ConstInt > index.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class BCEPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "BCE"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
