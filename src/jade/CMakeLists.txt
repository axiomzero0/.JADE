# SPDX-License-Identifier: MIT
# src/jade/CMakeLists.txt

# ── asmjit (x86-64 JIT backend) ────────────────────────────────────────────
# Pulled from third_party/asmjit/ (cloned at setup time).
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/asmjit/CMakeLists.txt")
    set(ASMJIT_EMBED ON CACHE BOOL "" FORCE)
    set(ASMJIT_TEST OFF CACHE BOOL "" FORCE)
    set(ASMJIT_NO_X86 OFF CACHE BOOL "" FORCE)
    set(ASMJIT_NO_A64 ON CACHE BOOL "" FORCE)   # we only target x86-64
    set(ASMJIT_NO_FOREIGN ON CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/asmjit third_party/asmjit EXCLUDE_FROM_ALL)
    set(JADE_HAVE_ASMJIT ON)
else()
    message(WARNING "asmjit not found at third_party/asmjit/ — Tier 1 JADE will not be built.")
    set(JADE_HAVE_ASMJIT OFF)
endif()

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
    jvm/Opcode.cpp
)
jade_target_apply_hot_path_flags(jade_core)

# ── Static library: jade_granit (Tier 0 interpreter + lowerers) ──────────
# NOT the JIT proper — exceptions are allowed here for runtime error reporting
# (granit errors are recoverable: abort interpretation, report to caller).
add_library(jade_granit STATIC
    tier0_granit/Bytecode.cpp
    tier0_granit/Value.cpp
    tier0_granit/Interpreter.cpp
    cil/Lowerer.cpp
    jvm/Lowerer.cpp
)
jade_target_apply_tooling_flags(jade_granit)
target_link_libraries(jade_granit PUBLIC jade_core)

# ── Static library: jade_tier1 (Tier 1 JADE — real x86-64 JIT) ────────────
# Built only if asmjit is available. Tier 1 is a functional baseline JIT:
#   - Linear Scan register allocation (Wimmer-Franz)
#   - asmjit-based code emission
#   - Falls back to granit on any unsupported NodeKind (No Stubs policy).
if(JADE_HAVE_ASMJIT)
    add_library(jade_tier1 STATIC
        tier1_jade/LinearScanRegAlloc.cpp
        tier1_jade/FrameLayout.cpp
        tier1_jade/SafepointEmitter.cpp
        tier1_jade/CodeEmitter.cpp
        tier1_jade/JadeJit.cpp
    )
    jade_target_apply_tooling_flags(jade_tier1)
    target_link_libraries(jade_tier1 PUBLIC jade_core)
    target_link_libraries(jade_tier1 PRIVATE asmjit::asmjit)
    target_compile_definitions(jade_tier1 PUBLIC JADE_HAVE_ASMJIT=1)
endif()

# ── CLI driver: jadec ─────────────────────────────────────────────────────
# Exceptions allowed for I/O and arg parsing.
add_executable(jadec driver/main.cpp driver/Driver.cpp)
jade_target_apply_tooling_flags(jadec)
target_link_libraries(jadec PRIVATE jade_core jade_granit)
if(JADE_HAVE_ASMJIT)
    target_link_libraries(jadec PRIVATE jade_tier1)
    target_compile_definitions(jadec PRIVATE JADE_HAVE_ASMJIT=1)
endif()

# ── Install ────────────────────────────────────────────────────────────────
install(TARGETS jadec jade_core jade_granit
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
if(JADE_HAVE_ASMJIT)
    install(TARGETS jade_tier1 ARCHIVE DESTINATION lib)
endif()
