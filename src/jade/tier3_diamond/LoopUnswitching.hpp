// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/LoopUnswitching.hpp
//
// Loop unswitching (Standard catalog §5.5).
// If a loop contains a loop-invariant conditional, hoist the condition
// outside the loop, duplicating the loop body.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade::tier3 {

class LoopUnswitchingPass final : public jade::Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "LoopUnswitching"; }
    [[nodiscard]] jade::Result<void> run(jade::Graph& g, jade::PassContext& ctx) override;
};

}  // namespace jade::tier3
