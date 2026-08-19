// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/LoopPeeling.hpp
//
// Loop peeling (Standard catalog §5.4).
// Peel the first N iterations out of a loop to enable specialization.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class LoopPeelingPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "LoopPeeling"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
