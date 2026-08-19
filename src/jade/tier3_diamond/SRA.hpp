// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/SRA.hpp
//
// Scalar Replacement of Aggregates.
//
// If an Allocate/NewObj node is proven non-escaping (by PEA/EA), eliminate
// the allocation entirely. Replace LoadField/StoreField on that object with
// direct SSA scalar values.
//
// For the initial milestone (no Phi insertion for partial escape), SRA
// only handles fully non-escaping allocations with field accesses.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade::tier3 {

class SRAPass final : public jade::Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "SRA"; }
    [[nodiscard]] jade::Result<void> run(jade::Graph& g, jade::PassContext& ctx) override;
};

}  // namespace jade::tier3
