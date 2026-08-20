# Bug 003: BuildRegions connect_edges doesn't handle Jump back-edges

**Root cause:** `BuildRegionsPass::connect_edges()` treated `Jump` nodes as
fall-through (the `default` case in its switch). This meant back-edges were
never created in the block successor/predecessor lists, so
`detect_loops()` never found any back-edges, so `is_loop_header` was never
set to `true` for any block.

This blocked the LSRA loop fix (`extend_intervals_across_loops` queries
`is_loop_header` to find loops). Without this fix, the LSRA extension was
a no-op even though the code was in place.

**Symptoms:** The LSRA loop extension code was correct but never executed
because `is_loop_header` was always `false`. The
`LoopWithRegisterInvariantBound` test failed until this bug was fixed.

**Fix:** Added a `case NodeKind::Jump:` to `connect_edges()` that scans all
nodes for a `Loop` header and creates a back-edge from the Jump's block to
the Loop header's block. This populates `successors`/`predecessors` correctly,
so `detect_loops()` finds the back-edge and sets `is_loop_header = true`.

**Commit:** d2692e9 (LSRA: loop-aware live interval extension + Jump back-edge in BuildRegions)

**Tests:**
1. `01_minimal_reproducer` — Smallest loop: verify is_loop_header is set.
2. `02_variant_trigger` — Multiple loops in one function.
3. `03_boundary_negative` — Forward Jump (no Loop header) should not crash.
4. `04_integration_contextual` — Realistic loop with multiple blocks.
5. `05_deopt_state_reconstruction` — Loop with safepoint; verify back-edge detection.
