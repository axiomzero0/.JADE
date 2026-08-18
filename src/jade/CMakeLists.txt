# SPDX-License-Identifier: MIT
# src/jade/CMakeLists.txt

# ── Static library: jade_core (the JIT hot path) ───────────────────────────
# Compiled with -fno-exceptions -fno-rtti (Rule B.2, B.3).
# This is the IR engine, the verifier, the optimization passes, and the
# runtime primitives (Safepoint, EBR).
add_library(jade_core STATIC
    ir/NodeKind.cpp
    ir/NodeFlag.cpp
    ir/TypeId.cpp
    ir/Graph.cpp
    ir/Verifier.cpp
    ir/passes/ConstantFolding.cpp
    ir/passes/DeadCodeElimination.cpp
    ir/passes/GVN.cpp
    ir/passes/PassPipeline.cpp

    cil/Opcode.cpp
)
jade_target_apply_hot_path_flags(jade_core)

# ── Static library: jade_granit (Tier 0 interpreter + CIL lowering) ──────
# NOT the JIT proper — exceptions are allowed here for runtime error reporting
# (granit errors are recoverable: abort interpretation, report to caller).
add_library(jade_granit STATIC
    tier0_granit/Bytecode.cpp
    tier0_granit/Value.cpp
    tier0_granit/Interpreter.cpp
    cil/Lowerer.cpp
)
jade_target_apply_tooling_flags(jade_granit)
target_link_libraries(jade_granit PUBLIC jade_core)

# ── CLI driver: jadec ─────────────────────────────────────────────────────
# Exceptions allowed for I/O and arg parsing.
add_executable(jadec driver/main.cpp driver/Driver.cpp)
jade_target_apply_tooling_flags(jadec)
target_link_libraries(jadec PRIVATE jade_core jade_granit)

# ── Install ────────────────────────────────────────────────────────────────
install(TARGETS jadec jade_core jade_granit
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
