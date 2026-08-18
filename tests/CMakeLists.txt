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
    unit/test_tier0_granit_interpreter.cpp
    unit/test_tier0_granit_safepoint.cpp
    unit/test_runtime_epoch.cpp
)

jade_target_apply_tooling_flags(jade_unit_tests)

target_link_libraries(jade_unit_tests PRIVATE
    jade_core
    jade_granit
    GTest::gtest_main
)

gtest_discover_tests(jade_unit_tests)

# ── Golden IR tests ─────────────────────────────────────────────────────────
# (Rule 37: ≥10 golden tests per pass before merging)
# Lives in tests/golden/. Each test is a .in.ir / .out.ir file pair.

# ── Differential testing ────────────────────────────────────────────────────
# (Rule 38: granit ↔ JADE ↔ RUBY ↔ DIAMOND byte-for-byte)
# Lives in tests/differential/. Not yet implemented.
