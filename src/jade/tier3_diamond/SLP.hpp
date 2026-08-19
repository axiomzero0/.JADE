// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/SLP.hpp
//
// Superword Level Parallelism (Larsen & Amarasinghe, 2000).
//
// Scans the SoN graph for adjacent, independent, isomorphic scalar operations
// (e.g., four separate Add nodes operating on array elements). Packs them
// into a single SIMD instruction.
//
// For the initial milestone, SLP detects candidate packs but does not yet
// emit SIMD code (that requires asmjit vector instruction support in the
// CodeEmitter). The pass marks packable nodes with a flag for the emitter.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade::tier3 {

class SLPPass final : public jade::Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "SLP"; }
    [[nodiscard]] bool is_analysis() const noexcept override { return true; }
    [[nodiscard]] jade::Result<void> run(jade::Graph& g, jade::PassContext& ctx) override;
};

}  // namespace jade::tier3
