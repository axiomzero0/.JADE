// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/Pass.hpp
//
// Base interface for IR passes. Every pass (Rule B.5) is idempotent and
// (Rule B.6) monotonic decreasing in IR size or moves toward a normal form.

#pragma once

#include "jade/core/Result.hpp"
#include "jade/ir/Graph.hpp"

#include <string_view>

namespace jade {

// PassContext — shared state for a pass invocation.
struct PassContext {
    // Maximum number of fixpoint iterations before a pass is forced to stop.
    uint32_t max_iterations{1000};
    // Whether to run the verifier after this pass (Rule 42).
    bool verify_after{true};
};

// Pass — base interface.
class Pass {
public:
    virtual ~Pass() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool is_analysis() const noexcept { return false; }

    // Run the pass to fixpoint (or until PassContext::max_iterations).
    // Returns an error if the pass introduced invalid IR.
    [[nodiscard]] virtual Result<void> run(Graph& g, PassContext& ctx) = 0;
};

}  // namespace jade
