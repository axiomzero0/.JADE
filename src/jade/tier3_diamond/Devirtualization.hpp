// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/Devirtualization.hpp
//
// Speculative devirtualization via Class Hierarchy Analysis (CHA).
//
// If a virtual method has only one implementation in the loaded class
// hierarchy, emit a direct call with a CheckClass guard.
// On guard failure, deopt to the generic virtual dispatch.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade::tier3 {

class DevirtualizationPass final : public jade::Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "Devirtualization"; }
    [[nodiscard]] jade::Result<void> run(jade::Graph& g, jade::PassContext& ctx) override;
};

}  // namespace jade::tier3
