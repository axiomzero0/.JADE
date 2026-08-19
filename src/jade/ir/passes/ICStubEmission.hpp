// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/ICStubEmission.hpp
//
// Monomorphic Inline Cache (IC) emission (Standard catalog §7.3).
//
// For CallVirt/Call with a single observed receiver class, emit:
//   cmp [obj + shape_offset], expected_shape
//   jne deopt
//   mov result, [obj + method_offset]  (or direct call)
//
// This is the Tier 1 (JADE) IC. Profile data from granit identifies
// the monomorphic target.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class ICStubEmissionPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "ICStubEmission"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
