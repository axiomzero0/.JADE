---
title: "Benchmark Record"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule 41", "Rule 47", "Rule 52"]
pass_type: "Architecture"
tier: "All"
---

# Benchmark Record

**Status:** Stable  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-19  
**Related Rules:** Rule 41 (Perf regression waiver), Rule 47 (Cost model), Rule 52 (Correctness-preserving fixes)

---

## 1. Why This Document Exists

This is the **Gold Standard history** of `.JADE` benchmark results. It exists to:

1. Prevent silent degradation (Rule 41).
2. Provide the data needed for waiver justifications.
3. Track the long-term performance impact of every change.

The script `tools/update_benchmark_record.py` (planned) appends a new row to this file after every commit. CI fails if any benchmark's ratio drops below `0.95` without an approved `WAIVER-NNN` reference.

---

## 2. Benchmarks

The benchmark suite lives in `benchmarks/` (planned) and is split by target language:

```
benchmarks/
├── csharp/             # C# / .NET 9
│   ├── mandelbrot.cs
│   ├── allocation_heavy.cs
│   ├── virtual_storm.cs
│   ├── generic_math.cs
│   ├── vector_simd.cs
│   └── ...
├── java/               # Java / OpenJDK 21
│   ├── mandelbrot.java
│   ├── allocation_heavy.java
│   ├── virtual_storm.java
│   ├── stream_pipeline.java
│   └── ...
└── shared/             # cross-language benchmarks
    ├── fib_recursive.{cs,java}
    └── matrix_mul.{cs,java}
```

Each benchmark produces two measurements:

- **Time (ms)** — wall-clock time.
- **Allocations (MB)** — peak heap allocated.

The "ratio vs baseline" column is the multiplier vs the reference runtime (.NET CLR for C#, OpenJDK for Java):

- `1.00x` = parity with the reference.
- `1.50x` = .JADE is 1.5× faster.
- `0.95x` = .JADE is 5% slower (regression — waiver required).

---

## 3. Record

| Date | Commit Hash | Tier | Benchmark Name | Target | Time (ms) | Allocations (MB) | Ratio vs Baseline | Notes / Waiver ID |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 2026-08-19 | `e580363` | T0 granit | (built-in demo) | C# | 0.05 | 0.0 | 0.01x | Initial scaffold; runs `(3+4)*5` only |
| 2026-08-19 | `e580363` | T2 RUBY | (built-in demo) | C# | 0.05 | 0.0 | 0.01x | ConstantFolding folds to ConstInt:35 |

> The first real benchmark rows will be added once the macro-benchmark suite is implemented (planned for sprint 3).

---

## 4. How to Run Benchmarks

### 4.1 Run a single benchmark

```bash
# C# / .NET 9 baseline
$ dotnet run -c Release --project benchmarks/csharp/mandelbrot.csproj

# Java / OpenJDK 21 baseline
$ javac benchmarks/java/mandelbrot.java && java -cp benchmarks/java mandelbrot

# .JADE (granit only)
$ ./build/bin/jadec --tier 0 benchmarks/csharp/mandelbrot.dll

# .JADE (RUBY)
$ ./build/bin/jadec --tier 2 benchmarks/csharp/mandelbrot.dll

# .JADE (DIAMOND)
$ ./build/bin/jadec --tier 3 benchmarks/csharp/mandelbrot.dll
```

### 4.2 Run the full suite

```bash
$ python3 tools/run_benchmarks.py
# (writes results to /tmp/bench-results-<commit-sha>.json)
```

### 4.3 Update this record

```bash
$ python3 tools/update_benchmark_record.py /tmp/bench-results-<commit-sha>.json
# (appends a row to docs/BENCHMARK_RECORD.md)
# (fails if any ratio < 0.95 without a WAIVER-NNN reference)
```

---

## 5. Mandatory Fields

Every entry **must** include:

| Field | Description |
| :-- | :-- |
| **Date** | YYYY-MM-DD when the benchmark was run. |
| **Commit Hash** | The git commit at which the benchmark was run. |
| **Tier** | Which tier produced the result: `granit`, `JADE`, `RUBY`, or `DIAMOND`. |
| **Benchmark Name** | The benchmark file's basename (e.g., `mandelbrot`). |
| **Target** | The source language and runtime: `C# (.NET 9)` or `Java (OpenJDK 21)`. |
| **Time (ms)** | Wall-clock time in milliseconds (median of 5 runs). |
| **Allocations (MB)** | Peak heap allocated (in MB). |
| **Ratio vs Baseline** | Time multiplier vs the reference runtime. `1.00x` = parity; `0.95x` = 5% regression. |
| **Notes / Waiver ID** | Free text. If ratio < 0.95, must include `WAIVER-NNN` reference. |

---

## 6. Waiver Process

If a benchmark regresses >5%, the PR must include:

1. A waiver document `docs/waivers/WAIVER-NNN.md`:
   - Root cause analysis.
   - Justification (e.g., "deopt path overhead; will be fixed in follow-up #NNN").
   - Tracking issue link.
   - Removal deadline (max 60 days).
2. A reviewer approval (PR comment `+1 waiver`).
3. The waiver ID added to the "Notes / Waiver ID" column of this record.

Waivers expire on their removal deadline. If the waiver is not removed (by either fixing the regression or re-approving), CI fails.

---

## 7. Cost Model Validation (Rule 47)

Every aggressive pass (Inlining, LoopUnrolling, SLP, PEA, etc.) must produce a `CostModelReport` per invocation. The report includes:

- Pass name.
- Input IR size (nodes).
- Output IR size (nodes).
- Projected speedup (from the cost model).
- Actual speedup (from the benchmark).

Discrepancies > 20% between projected and actual speedup are flagged in the "Notes" column.

---

## 8. Correctness Preservation (Rule 52)

A performance improvement that sacrifices correctness is **forbidden**. If a benchmark's ratio improves but the differential test (`tests/differential/`) shows any divergence, the PR is blocked from merge.

Every entry in this record implicitly satisfies:

- All `tests/differential/` programs pass byte-for-byte on the same tier.
- All `tests/golden/` IR tests pass.
- The graph verifier (Rule 42) passes after every pass.
