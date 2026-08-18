---
title: "Testing Doctrine"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule 36", "Rule 37", "Rule 38", "Rule 39", "Rule 40", "Rule 41", "Rule 42", "Rule 43"]
pass_type: "Architecture"
tier: "All"
---

# Testing Doctrine

**Status:** Stable  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-19  
**Related Rules:** Rule 36 (5 regression tests), 37 (Golden tests), 38 (Differential testing), 39 (Deopt fuzz), 40 (Replay logs), 41 (Perf waiver), 42 (Verifier), 43 (Test names)

---

## 1. Why This Document Exists

Compiler bugs are uniquely expensive because they corrupt trust. A user who gets wrong results silently will never come back. This document specifies the testing protocol that makes entire categories of bugs **impossible**:

- Rule 36: every bugfix ships with 5 regression tests.
- Rule 37: every pass has ≥10 golden IR tests.
- Rule 38: differential testing on every PR.
- Rule 42: graph verifier after every pass in debug builds.

If the implementation disagrees with this document, the document is correct.

---

## 2. Test Layout

```
tests/
├── unit/                       # Unit tests (GoogleTest)
│   ├── test_core_nodeid.cpp
│   ├── test_core_flags.cpp
│   ├── test_core_arena.cpp
│   ├── test_ir_graph.cpp
│   ├── test_ir_verifier.cpp
│   ├── test_ir_passes.cpp
│   ├── test_ir_csharp_nodes.cpp
│   ├── test_cil_opcode.cpp
│   ├── test_cil_lowerer.cpp
│   ├── test_jvm_opcode.cpp     # NEW (planned)
│   ├── test_jvm_lowerer.cpp    # NEW (planned)
│   ├── test_granit_value.cpp
│   ├── test_tier0_granit_interpreter.cpp
│   ├── test_tier0_granit_safepoint.cpp
│   └── test_runtime_epoch.cpp
│
├── golden/                     # Golden IR tests (Rule 37)
│   ├── constant_folding/
│   │   ├── 01_basic_add.in.ir
│   │   ├── 01_basic_add.out.ir
│   │   └── ...                 # ≥10 per pass
│   ├── gvn/
│   ├── dce/
│   └── (planned: cse, sccp, licm, gcm, pea, ...)
│
├── regression/                 # Bug-fix regression tests (Rule 36)
│   └── <issue-#>_<slug>/
│       ├── 01_minimal_reproducer.cil
│       ├── 02_variant_trigger.cil
│       ├── 03_boundary_negative.cil
│       ├── 04_integration_contextual.cil
│       └── 05_deopt_state_reconstruction.cil
│
├── differential/                # Differential tests (Rule 38)
│   └── programs/
│       ├── mandelbrot.cil      # C#
│       ├── mandelbrot.class    # Java
│       └── ...
│
├── cil/                        # Per-opcode CIL tests
│   ├── ldconst_nop.cil
│   ├── arithmetic.cil
│   ├── box_unbox.cil
│   └── ...
│
├── jvm/                        # Per-opcode JVM tests
│   ├── iconst.cil
│   ├── invokevirtual.cil
│   └── ...
│
└── replay/                     # Replay artifacts for CI failures (Rule 40)
    └── failed/
        └── <test-name>-<commit-sha>/
            ├── source.cil
            ├── profile.bin
            ├── frame_state.bin
            └── diff.txt
```

---

## 3. Rule 36 — 5 Regression Tests per Bug Fix

### 3.1 Template

Every bug fix PR must include 5 new test files in `tests/regression/<issue-#>_<slug>/`:

| # | Test Name | Purpose |
| :-- | :-- | :-- |
| 1 | `01_minimal_reproducer` | Smallest possible input that triggers the bug. |
| 2 | `02_variant_trigger` | Different code pattern exercising the same root cause. |
| 3 | `03_boundary_negative` | Ensures the fix doesn't over-correct (e.g., doesn't fire when it shouldn't). |
| 4 | `04_integration_contextual` | The bug in realistic surrounding code. |
| 5 | `05_deopt_state_reconstruction` | Verifies correctness under bailout (Rule A.4). |

Each test contains:
- `source.cil` (or `source.jvm`) — the input bytecode.
- `expected.txt` — the expected `granit` output.
- `README.md` — description of the root cause and how the test exercises it.

### 3.2 Enforcement

CI script `tools/check_regression_tests.py` (planned) scans PR labels:

```python
if "bugfix" in pr.labels:
    test_count = count_new_regression_tests(pr.diff)
    if test_count < 5:
        ci.fail(f"Rule 36 violation: bugfix PR has {test_count} regression tests; need ≥5")
```

No exceptions. PRs without 5 regression tests are blocked from merge.

### 3.3 Example test

`tests/regression/0421_gvn_misses_commutative_add/01_minimal_reproducer.cil`:

```
// Bug: GVN failed to dedup `a + b` with `b + a` due to missing commutative normalization.
.method public static int Test(int a, int b) {
  ldc.i4 3
  ldc.i4 4
  add             // produces 7
  ldc.i4 4
  ldc.i4 3
  add             // should dedup with the previous add
  ret
}
```

`tests/regression/0421_gvn_misses_commutative_add/01_minimal_reproducer.expected.txt`:

```
int32:7
```

`tests/regression/0421_gvn_misses_commutative_add/README.md`:

```markdown
# Bug: GVN missed commutative duplicates

**Root cause:** `NodeSignature::inputs` was not being sorted for nodes with the `Commutative` flag.

**Fix:** Sort inputs in `make_signature()` when `n.flags.has(NodeFlag::Commutative)`.

**Test 1 (this file):** Smallest reproducer — `a + b` and `b + a` should produce one node, not two.
**Test 2:** Variant with three operands.
**Test 3:** Boundary: ensure non-commutative ops (Sub) are NOT deduped.
**Test 4:** Integration: the bug in a real method (matrix multiplication).
**Test 5:** Deopt: force a deopt between the two adds and verify byte-for-byte identical output.
```

---

## 4. Rule 37 — Golden IR Tests

### 4.1 Format

Golden tests are `.in.ir` / `.out.ir` file pairs in `tests/golden/<pass_name>/`:

```
tests/golden/gvn/basic_add.in.ir
─────────────────────────────────
# input: %1 = ConstInt 3
# input: %2 = ConstInt 4
# input: %3 = Add %1 %2
# input: %4 = Add %1 %2  # duplicate
# input: %5 = Return %3

tests/golden/gvn/basic_add.out.ir
─────────────────────────────────
# output: %1 = ConstInt 3
# output: %2 = ConstInt 4
# output: %3 = Add %1 %2
# output: %5 = Return %3
```

### 4.2 Verification

CI script `tools/check_golden_tests.py` (planned):

1. For each `tests/golden/<pass>/` directory:
   - For each `.in.ir` file:
     - Load the IR.
     - Run the pass to fixpoint.
     - Dump the resulting IR.
     - Compare against the `.out.ir` file (byte-for-byte after normalizing whitespace).
2. If the diff is non-empty, fail CI.

### 4.3 Coverage check

CI script `tools/check_pass_coverage.py` (planned):

1. Parse `docs/PASS_LIST.md` to get the list of implemented passes.
2. For each pass, check that `tests/golden/<pass_name>/` has ≥10 `.in.ir` files.
3. Fail CI if any pass is under-covered.

### 4.4 Adding a new golden test

```bash
$ cat > tests/golden/gvn/commutative_add.in.ir <<'EOF'
%1 = ConstInt 5
%2 = ConstInt 7
%3 = Add %1 %2
%4 = Add %2 %1   # should dedup
%5 = Return %3
EOF

$ ./build/bin/jade_unit_tests --gtest_filter=GoldenTest.gvn_commutative_add
# (writes the actual output to tests/golden/gvn/commutative_add.out.ir.actual)
$ diff tests/golden/gvn/commutative_add.out.ir tests/golden/gvn/commutative_add.out.ir.actual
# (review the diff; if correct, copy .actual to .out.ir and commit)
```

---

## 5. Rule 38 — Differential Testing

### 5.1 What it does

For every program in `tests/differential/programs/`:

1. Run on `granit` (the reference interpreter). Capture stdout, exit code, heap-state digest.
2. Run on `JADE`. Capture identical artifacts.
3. Run on `RUBY`. Capture identical artifacts.
4. Run on `DIAMOND`. Capture identical artifacts.

Any divergence (even one byte) fails CI.

### 5.2 Targets

Differential testing is run against two reference runtimes:

| Source | Reference runtime |
| :-- | :-- |
| C# | .NET 9 CLR (`dotnet run`) |
| Java | OpenJDK 21 (`java -cp ...`) |

### 5.3 Example

`tests/differential/programs/fibonacci.cil` (C# source compiled to CIL):

```cil
.method public static int Fib(int n) {
  ldarg.0
  ldc.i4.2
  blt.s L1
  ldarg.0
  ldc.i4.1
  sub
  call int Fib(int)
  ldarg.0
  ldc.i4.2
  sub
  call int Fib(int)
  add
  ret
L1:
  ldarg.0
  ret
}
```

`tests/differential/programs/fibonacci.expected.txt`:

```
 granit: 55
 JADE:   55
 RUBY:   55
DIAMOND: 55
.NET CLR: 55
```

### 5.4 CI script

`tools/run_differential.py` (planned) runs the matrix and produces a diff. Any non-empty diff blocks merge.

---

## 6. Rule 39 — Deopt Path Fuzzing

### 6.1 Schedule

A scheduled CI job (`.github/workflows/deopt-fuzz.yml`) runs weekly:

```yaml
on:
  schedule:
    - cron: '0 4 * * 0'   # every Sunday 04:00 UTC
```

### 6.2 What it does

1. Generate 1000 random C# / Java programs (using a grammar-aware generator).
2. For each program:
   - Run on `granit`.
   - Run on `JADE`/`RUBY`/`DIAMOND` with **random guard injection** (force deopts at random points).
   - Assert byte-for-byte identical output.
3. Results triaged within 24 hours.
4. Untriaged deopt fuzz failures block releases.

### 6.3 Random guard injection

The fuzzer picks random points in the JIT-compiled code and patches a guard's success path to call `deopt_handler` instead. This forces the deopt path to execute, exercising the FrameState reconstruction logic.

---

## 7. Rule 40 — Replay Logs for CI Failures

When a CI run fails, the full state is saved to `tests/replay/failed/<test-name>-<commit-sha>/`:

```
tests/replay/failed/<test-name>-<commit-sha>/
├── source.cil             # original CIL bytecode
├── source.class           # original JVM bytecode (if Java)
├── bytecode.bin           # as compiled by granit
├── profile.bin            # profile data collected
├── options.json           # compiler options
├── rng_seed.txt           # RNG seed used by JIT
├── frame_state.bin        # the FrameState blob at the deopt point (if deopt)
├── register_state.bin     # raw register values (if deopt)
├── ir_after_each_pass/    # snapshot of IR after every pass
├── granit_output.txt      # reference output
├── jit_output.txt        # actual (incorrect) output
└── diff.txt              # the observable diff
```

Bug triage starts from these artifacts, not from reproduction. The CI runner uploads the replay directory as a build artifact with a 30-day retention.

---

## 8. Rule 41 — Performance Regression Waiver

### 8.1 Threshold

If a benchmark regresses **>5%** (ratio < 0.95), the PR is blocked from merge.

### 8.2 Waiver process

1. The PR author writes a waiver document `docs/waivers/WAIVER-<NNN>.md`:
   - Root cause analysis.
   - Justification (e.g., "deopt path overhead; will be fixed in follow-up #1234").
   - Tracking issue link.
   - Removal deadline (max 60 days).
2. A reviewer approves the waiver.
3. The waiver ID is added to the `Notes/Waiver ID` column of `docs/BENCHMARK_RECORD.md`.
4. The waiver expires on its removal deadline; if not removed, CI fails.

### 8.3 Example waiver

`docs/waivers/WAIVER-001.md`:

```markdown
# WAIVER-001: Deopt path overhead in Virtual Storm

**Status:** Approved (2026-08-20)
**Author:** JADE Dev Team
**Tracking issue:** #1234
**Removal deadline:** 2026-10-20

## Root cause
The deopt handler does a linear scan of the FrameState table. With 64 simultaneous
deopting threads, this becomes O(N) per deopt.

## Justification
The fix (hash table) is planned for sprint 4. Until then, the regression is acceptable
because the affected benchmark (`Virtual Storm`) is not in the customer workload.

## Performance plan
Sprint 4 will replace the linear scan with a hash table. Expected speedup: 1.5x.
```

---

## 9. Rule 42 — Graph Verifier

### 9.1 When it runs

In debug builds (`-DCMAKE_BUILD_TYPE=Debug`), the verifier runs after **every** pass:

```cpp
Result<void> Pass::run(Graph& g, PassContext& ctx) {
    auto r = run_impl(g, ctx);
    if (!r) return r;
    if (verifier_enabled()) {
        if (auto v = verify_graph(g); !v) return std::unexpected(v.error());
    }
    return {};
}
```

In release builds, the verifier is skipped (it's expensive — O(N²) in the worst case).

### 9.2 What it checks

The verifier (`src/jade/ir/Verifier.cpp`) checks these invariants:

1. **No dangling NodeIds** — every input/output edge resolves to a valid `Node` in the same graph.
2. **Effect chain continuity** — every `Effect` node has exactly one effect input; chains terminate at `Start` or a `Loop` phi.
3. **Control dominance** — every node's control input dominates the node itself (simplified check: control input exists and is reachable from `Start`).
4. **Use-def consistency** — if `A` has input `B`, then `B`'s output list contains `A` (and vice versa).
5. **No dead nodes with live users** — if `A` is marked `IsDead`, no live node may reference it.
6. **FrameState attached to every guard** — every node with `IsGuard` flag must have a non-null `FrameStateId` (Rule A.3).

### 9.3 Failure mode

If the verifier fails, the pass returns a `VerificationFailed` error. The pipeline aborts compilation for that method and falls back to `granit` (Rule C.1 — never block the mutator).

### 9.4 CI enforcement

CI runs the entire test suite in debug mode (`-DCMAKE_BUILD_TYPE=Debug`). Any test that triggers a verifier failure blocks merge.

---

## 10. Rule 43 — Test Names

Test names must encode the bug or feature they cover.

| Bad | Good |
| :-- | :-- |
| `test_pea_3` | `pea_non_escaping_object_with_deopt_materializes_correctly` |
| `test_gvn_42` | `gvn_dedupes_commutative_add_with_swapped_operands` |
| `test_verify` | `verifier_catches_missing_framestate_on_guard` |

CI script `tools/check_test_names.py` (planned) scans for test names matching `test_\w+_\d+` and fails CI.

---

## 11. CI Layout

```
.github/workflows/
├── ci.yml                  # Fast: build + unit + golden tests on every push (debug + release)
├── differential.yml        # Nightly: granit↔JADE↔RUBY↔DIAMOND↔.NET↔OpenJDK
├── deopt-fuzz.yml          # Weekly: deopt path fuzzer
├── perf-bench.yml          # Nightly: macro benchmarks, regression check (Rule 41)
└── coverage.yml            # Weekly: opcode coverage, pass coverage, golden coverage
```

---

## 12. Test Discovery

Tests are auto-discovered via CTest + GoogleTest:

```cmake
include(GoogleTest)
gtest_discover_tests(jade_unit_tests)
```

To run a single test:

```bash
$ ./build/bin/jade_unit_tests --gtest_filter='GVNTest.DeduplicatesIdenticalAdds'
```

To list all tests:

```bash
$ ./build/bin/jade_unit_tests --gtest_list_tests
```
