// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/PassPipeline.cpp

#include "jade/ir/passes/PassPipeline.hpp"
#include "jade/ir/passes/ConstantFolding.hpp"
#include "jade/ir/passes/DeadCodeElimination.hpp"
#include "jade/ir/passes/GVN.hpp"
#include "jade/ir/passes/SCCP.hpp"
#include "jade/ir/passes/CSE.hpp"
#include "jade/ir/passes/AlgebraicSimplification.hpp"
#include "jade/ir/passes/ControlFlowSimplification.hpp"
#include "jade/ir/passes/LICM.hpp"
#include "jade/ir/passes/GCM.hpp"
#include "jade/ir/passes/BCE.hpp"
#include "jade/ir/passes/NCE.hpp"
#include "jade/ir/passes/EscapeAnalysis.hpp"

namespace jade {

std::unique_ptr<PassPipeline> build_ruby_pipeline() {
    auto pipe = std::make_unique<PassPipeline>();
    // Order matters: cheap simplifications first, then expensive passes.
    pipe->add(std::make_unique<AlgebraicSimplificationPass>());
    pipe->add(std::make_unique<SCCPPass>());
    pipe->add(std::make_unique<ConstantFoldingPass>());
    pipe->add(std::make_unique<CSEPass>());
    pipe->add(std::make_unique<GVNPass>());
    pipe->add(std::make_unique<ControlFlowSimplificationPass>());
    pipe->add(std::make_unique<LICMPass>());
    pipe->add(std::make_unique<BCEPass>());
    pipe->add(std::make_unique<NCEPass>());
    pipe->add(std::make_unique<EscapeAnalysisPass>());
    pipe->add(std::make_unique<DeadCodeEliminationPass>());
    pipe->add(std::make_unique<GCMPass>());
    return pipe;
}

}  // namespace jade
