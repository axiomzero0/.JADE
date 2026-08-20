# Bug 002: LSRA doesn't extend live intervals across loop back-edges

**Root cause:** The Linear Scan Register Allocator computed live intervals
based on linear NodeId positions. It didn't know that the loop body
re-executes. This caused a correctness bug: a value defined before a loop
and used inside it (e.g., a `ConstInt` loop bound) got a live interval that
ended at its last use. The LSRA then reused that register for a different
value inside the loop body. When the `Jump` back-edge re-entered the loop
header, the register no longer held the original value.

**Symptoms:** The `CountToFiveLoop` test (with `ConstInt(5)` as a raw
register-allocated invariant) returned 1 instead of 5 — the loop ran once
but the comparison `i < 5` used a clobbered register on the second iteration,
so the loop exited early.

**Fix:** Added `LinearScanRegAlloc::extend_intervals_across_loops()`, called
after the initial live interval computation. This method uses BuildRegions
to identify loop headers and back-edges, then extends the live interval of
any value defined before a loop and used inside it to cover the entire loop
body. It also increases the spill weight by 10.0 to discourage spilling
loop-invariant values.

**Commit:** d2692e9 (LSRA: loop-aware live interval extension)

**Tests:**
1. `01_minimal_reproducer` — Smallest loop with register-allocated invariant.
2. `02_variant_trigger` — Loop with multiple register-allocated invariants.
3. `03_boundary_negative` — Loop with zero iterations (cond false at entry).
4. `04_integration_contextual` — Nested loop with register-allocated invariants.
5. `05_deopt_state_reconstruction` — Loop with a guard that could deopt.
