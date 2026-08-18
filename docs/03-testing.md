# Testing, Debugging, and Regression Rules

Compiler bugs are uniquely expensive because they corrupt trust. Users who get wrong results silently will never come back. These rules make entire categories of bugs impossible.

---

## Summary of Rules

| Rule | Title | Enforcement |
| :-- | :-- | :-- |
| 36 | Five regression tests per bug fix | CI fails PR if bugfix has < 5 new tests |
| 37 | Golden tests for every pass | ≥ 10 `.in.ir`/`.out.ir` pairs per pass |
| 38 | Differential testing mandatory in CI | `granit`↔`JADE`↔`RUBY`↔`DIAMOND` byte-identical |
| 39 | Deopt paths fuzzed weekly | Scheduled job, 24-hour triage SLA |
| 40 | Replay logs retained for CI failures | bytecode + profile + options + RNG seed |
| 41 | Performance regressions need waiver | >5% regression → RCA + tracking issue + approval |
| 42 | Graph verifier after every pass | Debug builds only; mandatory |
| 43 | Test names encode what they cover | `pea_non_escaping_object_with_deopt_materializes_correctly` |
| 44 | No assumption without invalidation | Watchdog registry + Trip path |
| 45 | No specialization without fallback | Generic fallback + deopt + downgrade + budget |
| 46 | No profile data without confidence | count + stability + age + decay + variance + deopt correlation |
| 47 | No aggressive pass without cost model | Inlining/unrolling/vectorization/etc. |
| 48 | No FFI optimization without ABI proof | calling convention + alignment + clobbering + transitions + ownership |
| 49 | No vectorization without dependence proof | aliasing + bounds + alignment + remainder + scalar fallback |
| 50 | No persistent state without versioning | profile/code caches, snapshots, AOT artifacts |
| 51 | All orthogonal boolean state bitmasked | `Flags<E>` wrapper, symbolic printing, debugger visualizer |
| 52 | No easy fixes | correctness-preserving performance fixes only |

---

## CI Layout

```
.github/workflows/
├── ci.yml                  # Fast: build + unit + golden tests on every push
├── differential.yml        # Nightly: granit↔JADE↔RUBY↔DIAMOND
├── deopt-fuzz.yml          # Weekly: deopt path fuzzer
└── perf-bench.yml          # Nightly: macro benchmarks, regression check
```

---

## Test Layout

```
tests/
├── unit/
│   ├── core/               # Arena, Result, NodeId, Flags
│   ├── ir/                 # Graph, Verifier, Edge pool
│   ├── tier0_granit/       # Interpreter opcode tests
│   ├── tier1_jade/         # Baseline JIT tests
│   ├── tier2_ruby/         # Optimizing JIT tests
│   └── tier3_diamond/      # PEA / SLP / vectorization
├── golden/
│   ├── gvn/
│   │   ├── basic_add.in.ir
│   │   ├── basic_add.out.ir
│   │   └── ...             # ≥10 per pass
│   ├── pea/
│   ├── licm/
│   └── ...
├── differential/
│   └── programs/           # .jade source programs; output matched across tiers
└── replay/
    └── failed/             # replay artifacts for failed CI runs
```

---

## Rule 36 — Required Regression Test Templates

For every bugfix, the 5 mandatory tests must be in their own folder:

```
tests/regression/<issue-#>_<short-slug>/
├── 01_minimal_reproducer.jade         # smallest input that triggers the bug
├── 02_variant_trigger.jade            # different code pattern, same root cause
├── 03_boundary_negative.jade          # fix must not over-correct
├── 04_integration_contextual.jade     # realistic surrounding code
└── 05_deopt_state_reconstruction.jade # verifies correctness under bailout
```

Each test must include:
- The `.jade` source input.
- The expected `granit` output (golden).
- A `.md` file describing the root cause and how the test exercises it.

---

## Rule 37 — Golden Test Format

Golden tests are checked-in `.in.ir` / `.out.ir` pairs. The `.in.ir` is fed into a pass; the `.out.ir` is the expected IR after the pass runs to fixpoint.

```
tests/golden/gvn/basic_add.in.ir
─────────────────────────────────
#-input: %1 = ConstInt 3
#input:  %2 = ConstInt 4
#input:  %3 = Add %1 %2
#input:  %4 = Add %1 %2  # duplicate
#input:  %5 = Return %3

tests/golden/gvn/basic_add.out.ir
─────────────────────────────────
#output: %1 = ConstInt 3
#output: %2 = ConstInt 4
#output: %3 = Add %1 %2
#output: %5 = Return %3
```

A CI script diffs the actual `.out.ir` produced by the pass against the golden. Any diff fails CI.

---

## Rule 42 — Verifier Checklist

The verifier runs after *every* pass in debug builds. It checks:

1. **No dangling `NodeId`s** — every input/output edge resolves to a valid `Node` in the same graph.
2. **Effect chain continuity** — every `Effect` node has exactly one effect input; chains terminate at `Start` or `Loop` phi.
3. **Control dominance** — every node's control input dominates the node itself.
4. **Use-def consistency** — if `A` has input `B`, then `B`'s output list contains `A` (and vice versa).
5. **No dead nodes with live users** — if `A` is marked `IsDead`, no live node may reference it.
6. **`FrameState` attached to every guard** — every node with `IsGuard` flag must have a non-null `FrameStateId`.

---

## Rule 38 — Differential Testing

For every program in `tests/differential/programs/*.jade`:

1. Run on `granit`. Capture stdout, exit code, heap state digest.
2. Run on `JADE`. Capture identical artifacts.
3. Run on `RUBY`. Capture identical artifacts.
4. Run on `DIAMOND`. Capture identical artifacts.

Any divergence (even one byte) fails CI. The diff is dumped for triage.

---

## Rule 40 — Replay Artifacts

On CI failure, automatically save:

```
tests/replay/failed/<test-name>-<commit-sha>/
├── source.jade           # original input
├── bytecode.bin          # as compiled by granit
├── profile.bin           # profile data collected
├── options.json          # compiler options
├── rng_seed.txt          # RNG seed used by JIT
├── ir_after_each_pass/   # snapshot of IR after every pass
└── stderr.log            # full compiler stderr
```

Bug triage starts from these artifacts, not from reproduction.
