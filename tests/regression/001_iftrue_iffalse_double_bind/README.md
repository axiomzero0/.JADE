# Bug 001: IfTrue/IfFalse double-bind labels in CodeEmitter

**Root cause:** In the block-scheduled CodeEmitter, the `IfTrue` and `IfFalse`
node handlers were re-binding the block leader label via `a.bind(it->second)`,
even though the label was already bound at the start of the block during the
RPO walk. asmjit treats double-binding a label as an error, which silently
corrupted the emitted machine code.

**Symptoms:** Branch tests passed (both branches returned their respective
values), but loop tests failed — the loop body ran once but never iterated.
The double-binding caused asmjit to emit incorrect jump targets, so the
back-edge `jmp loop_header_label` jumped to the wrong address.

**Fix:** Removed the `a.bind()` calls from the `IfTrue` and `IfFalse` cases
in `CodeEmitter.cpp`. The labels are bound exactly once, at the start of
each block in the RPO walk.

**Commit:** 664538f (Real loop iteration: counted loops now execute in the JIT)

**Tests:**
1. `01_minimal_reproducer` — Smallest if-then-else with both branches returning.
2. `02_variant_trigger` — Nested if-then-else (if inside if).
3. `03_boundary_negative` — Single-branch if (no else) should NOT crash.
4. `04_integration_contextual` — Realistic function with multiple branches + arithmetic.
5. `05_deopt_state_reconstruction` — Branch with a guard; verify no crash under bailout.
