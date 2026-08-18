// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/GVN.hpp
//
// Global Value Numbering (advanced catalog §2.2):
//   Hash-based value numbering across the entire SoN graph.
//   Commutative operations are normalized (sort inputs by NodeId) before
//   hashing to catch `a + b` == `b + a`.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class GVNPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "GVN"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
