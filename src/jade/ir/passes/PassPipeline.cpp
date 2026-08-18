// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/PassPipeline.cpp

#include "jade/ir/passes/PassPipeline.hpp"
#include "jade/ir/passes/ConstantFolding.hpp"
#include "jade/ir/passes/DeadCodeElimination.hpp"
#include "jade/ir/passes/GVN.hpp"

namespace jade {

std::unique_ptr<PassPipeline> build_ruby_pipeline() {
    auto pipe = std::make_unique<PassPipeline>();
    pipe->add(std::make_unique<ConstantFoldingPass>());
    pipe->add(std::make_unique<GVNPass>());
    pipe->add(std::make_unique<DeadCodeEliminationPass>());
    return pipe;
}

}  // namespace jade
