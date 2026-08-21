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
#include "jade/ir/passes/Inlining.hpp"
#include "jade/ir/passes/TypeNarrowing.hpp"
#include "jade/ir/passes/TailCallElimination.hpp"
#include "jade/ir/passes/LoopPeeling.hpp"
#include "jade/ir/passes/Peephole.hpp"
#include "jade/ir/passes/ProfileGuidedBlockReorder.hpp"
#include "jade/ir/passes/ICStubEmission.hpp"
#include "jade/tier3_diamond/PEA.hpp"
#include "jade/tier3_diamond/SRA.hpp"
#include "jade/tier3_diamond/SLP.hpp"
#include "jade/tier3_diamond/LoopUnrolling.hpp"
#include "jade/tier3_diamond/LoopUnswitching.hpp"
#include "jade/tier3_diamond/Devirtualization.hpp"

namespace jade {

std::unique_ptr<PassPipeline> build_ruby_pipeline() {
    auto pipe = std::make_unique<PassPipeline>();
    pipe->add(std::make_unique<AlgebraicSimplificationPass>());
    pipe->add(std::make_unique<SCCPPass>());
    pipe->add(std::make_unique<ConstantFoldingPass>());
    pipe->add(std::make_unique<CSEPass>());
    pipe->add(std::make_unique<GVNPass>());
    pipe->add(std::make_unique<ControlFlowSimplificationPass>());
    pipe->add(std::make_unique<TypeNarrowingPass>());
    pipe->add(std::make_unique<LICMPass>());
    pipe->add(std::make_unique<LoopPeelingPass>());
    pipe->add(std::make_unique<BCEPass>());
    pipe->add(std::make_unique<NCEPass>());
    pipe->add(std::make_unique<EscapeAnalysisPass>());
    pipe->add(std::make_unique<InliningPass>());
    pipe->add(std::make_unique<TailCallEliminationPass>());
    pipe->add(std::make_unique<ICStubEmissionPass>());
    pipe->add(std::make_unique<DeadCodeEliminationPass>());
    pipe->add(std::make_unique<PeepholePass>());
    pipe->add(std::make_unique<GCMPass>());
    pipe->add(std::make_unique<ProfileGuidedBlockReorderPass>());
    return pipe;
}

std::unique_ptr<PassPipeline> build_diamond_pipeline() {
    auto pipe = build_ruby_pipeline();
    // DIAMOND tier: each pass that modifies the graph is followed by DCE
    // to clean up dead nodes, enabling further optimizations in the next pass.
    pipe->add(std::make_unique<tier3::PEAPass>());
    pipe->add(std::make_unique<DeadCodeEliminationPass>());
    pipe->add(std::make_unique<tier3::SRAPass>());
    pipe->add(std::make_unique<DeadCodeEliminationPass>());
    pipe->add(std::make_unique<tier3::LoopUnrollingPass>());
    pipe->add(std::make_unique<DeadCodeEliminationPass>());
    pipe->add(std::make_unique<tier3::LoopUnswitchingPass>());
    pipe->add(std::make_unique<DeadCodeEliminationPass>());
    pipe->add(std::make_unique<tier3::DevirtualizationPass>());
    pipe->add(std::make_unique<DeadCodeEliminationPass>());
    pipe->add(std::make_unique<tier3::SLPPass>());
    pipe->add(std::make_unique<DeadCodeEliminationPass>());
    // Final GCM + DCE to clean up everything.
    pipe->add(std::make_unique<GCMPass>());
    pipe->add(std::make_unique<DeadCodeEliminationPass>());
    return pipe;
}

}  // namespace jade
