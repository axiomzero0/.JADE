# SPDX-License-Identifier: MIT
# tests/CMakeLists.txt

include(GoogleTest)

# ── Unit tests ─────────────────────────────────────────────────────────────
add_executable(jade_unit_tests
    unit/test_core_nodeid.cpp
    unit/test_core_flags.cpp
    unit/test_core_arena.cpp
    unit/test_ir_graph.cpp
    unit/test_ir_verifier.cpp
    unit/test_ir_passes.cpp
    unit/test_ir_csharp_nodes.cpp
    unit/test_cil_opcode.cpp
    unit/test_cil_lowerer.cpp
    unit/test_jvm_opcode.cpp
    unit/test_jvm_lowerer.cpp
    unit/test_granit_value.cpp
    unit/test_tier0_granit_interpreter.cpp
    unit/test_tier0_granit_safepoint.cpp
    unit/test_runtime_epoch.cpp
    unit/test_tier1_jade.cpp
    unit/test_tier2_ruby_passes.cpp
    unit/test_tier3_diamond.cpp
    unit/test_pea_materialize.cpp
    unit/test_build_regions.cpp
    unit/test_granit_cil_interpreter.cpp
    unit/test_granit_jvm_interpreter.cpp
)

jade_target_apply_tooling_flags(jade_unit_tests)

target_link_libraries(jade_unit_tests PRIVATE
    jade_core
    jade_granit
    GTest::gtest_main
)

# Tier 1 tests need jade_tier1 (which pulls in asmjit).
if(TARGET jade_tier1)
    target_link_libraries(jade_unit_tests PRIVATE jade_tier1)
    target_compile_definitions(jade_unit_tests PRIVATE JADE_HAVE_ASMJIT=1)
endif()

gtest_discover_tests(jade_unit_tests)

# ── Golden IR tests ─────────────────────────────────────────────────────────
# (Rule 37: ≥10 golden tests per pass before merging)
# Lives in tests/golden/. Each test is a .in.ir / .out.ir file pair.

# ── Differential testing ────────────────────────────────────────────────────
# (Rule 38: granit ↔ JADE ↔ RUBY ↔ DIAMOND byte-for-byte)
# Lives in tests/differential/. Not yet implemented.
