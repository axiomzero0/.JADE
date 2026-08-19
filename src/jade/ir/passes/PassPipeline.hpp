// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/PassPipeline.hpp
//
// A simple pass pipeline that runs a list of passes to fixpoint.

#pragma once

#include "jade/ir/passes/Pass.hpp"
#include <memory>
#include <vector>

namespace jade {

class PassPipeline {
public:
    void add(std::unique_ptr<Pass> p) { passes_.push_back(std::move(p)); }

    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) {
        for (auto& p : passes_) {
            auto r = p->run(g, ctx);
            if (!r) return std::unexpected(r.error());
        }
        return {};
    }

    [[nodiscard]] std::size_t size() const noexcept { return passes_.size(); }

private:
    std::vector<std::unique_ptr<Pass>> passes_;
};

// Build the standard Tier 2 (RUBY) pipeline.
[[nodiscard]] std::unique_ptr<PassPipeline> build_ruby_pipeline();

// Build the standard Tier 3 (DIAMOND) pipeline. Includes all RUBY passes
// plus PEA, SRA, SLP.
[[nodiscard]] std::unique_ptr<PassPipeline> build_diamond_pipeline();

}  // namespace jade
