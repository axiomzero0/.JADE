---
title: "No Stubs / No Placeholders Policy"
status: "Mandatory"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule 52", "Rule B.1"]
pass_type: "Policy"
tier: "All"
---

# No Stubs / No Placeholders Policy

**Status:** Mandatory  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-19  
**Related Rules:** Rule 52 (No Easy Fixes), Rule B.1 (No malloc in hot path), Definition of Done

---

## 1. Core Principle

In `.JADE`, a "stub" or "TODO" in a critical path is not a reminder; it is a **compilation error**. We do not build "skeletons" and fill them in later. We build **functional, verified components** that may be limited in scope but are complete in implementation.

If a component cannot be fully implemented due to complexity, it must be **gated behind a feature flag** or **fallback tier**, not left as a runtime crash or a silent no-op.

---

## 2. Definition of "Critical Parts"

The following areas are strictly forbidden from containing stubs, placeholders, or unimplemented logic:

| Area | Forbidden Patterns | Required Implementation |
| :--- | :--- | :--- |
| **Memory Management** | `// TODO: implement free()` | Full Arena bump-pointer logic with bulk-free. |
| **IR Verification** | `return true; // skip check` | Full traversal of all 6 invariants (Rule 42). |
| **CIL/JVM Decoding** | `case OpCodes.X: break;` | Full operand decoding and stack transition logic. |
| **Safepoints** | `// TODO: poll GC` | Atomic load of the global safepoint flag at every back-edge. |
| **Deoptimization** | `// TODO: reconstruct state` | Full `FrameState` capture and interpreter handoff logic. |
| **Register Allocation** | `// TODO: spill to stack` | Functional Linear Scan with basic spill/reload logic. |
| **Code Emission** | `// TODO: emit prologue` | Real asmjit emission of prologue, body, epilogue. |
| **IC Stubs** | `// TODO: emit guard` | Real shape/class check + fast path + deopt miss. |

---

## 3. The "Fallback Tier" Rule

If a high-tier optimization (e.g., PEA in `DIAMOND`) is not yet ready, the system must **gracefully degrade** to a lower tier (`RUBY` or `JADE`) rather than using a stub.

*   **Bad:** `if (isComplex) { /* TODO: PEA */ return; }`
*   **Good:** `if (!pea_pass_ready) { return ruby_pipeline.compile(graph); }`

The same applies to Tier 1: if a specific NodeKind cannot be lowered to machine code yet, the entire compilation must abort and the method must run in `granit` (Tier 0). This is enforced by the `JadeJit::compile()` driver — see `src/jade/tier1_jade/JadeJit.cpp`.

---

## 4. Enforcement in CI

1.  **Static Analysis:** The CI pipeline runs `tools/check_no_stubs.py`, which searches for `TODO`, `FIXME`, `STUB`, `PLACEHOLDER`, `XXX`, and `not implemented` in any file within `src/jade/tier*/`, `src/jade/core/`, `src/jade/ir/`, `src/jade/cil/`, `src/jade/jvm/`, and `src/jade/runtime/`.
2.  **Build Failure:** If any of these keywords are found in a critical path, the build fails immediately.
3.  **Exception Process:** The only way to bypass this is to add a `// NOLINT(no-stubs)` comment accompanied by a link to a **Rule 41 Waiver** (`docs/waivers/WAIVER-NNN.md`) explaining why a temporary placeholder is necessary for bootstrapping.

### Allowed patterns

The following are **not** considered stubs:

- A `default:` case in a switch that returns `unsupported` ErrorKind (this is graceful degradation, not a stub).
- A feature flag check that falls back to a lower tier (e.g., `if (!pea_ready) return ruby.compile(g);`).
- A test that exercises a subset of functionality (tests are not production code).
- A `// Note:` or `// Note:` comment explaining design rationale.

### Forbidden patterns

- `// TODO: ...`
- `// FIXME: ...`
- `// STUB: ...`
- `// PLACEHOLDER: ...`
- `// XXX: ...`
- `// not implemented`
- `return Result::success(...);` with no actual work done
- Empty function bodies (other than defaulted constructors/destructors)

---

## 5. Rationale

Stubs create **technical debt** and **silent failures**. In a compiler, a silent failure means generating incorrect machine code. By enforcing this policy, we ensure that every commit to `.JADE` represents a **measurable, functional improvement** over the previous one.

A JIT that "almost works" is worse than no JIT at all, because users will trust the output. We would rather fall back to `granit` (slower but correct) than emit incorrect machine code.

---

## 6. Audit

The CI scraper produces a report at `build/no_stubs_report.txt`. A clean run looks like:

```
$ python3 tools/check_no_stubs.py
Scanning src/jade/ for stubs...
  - tier1_jade/JadeJit.cpp: clean
  - tier1_jade/LinearScanRegAlloc.cpp: clean
  - tier1_jade/CodeEmitter.cpp: clean
  - ir/Graph.cpp: clean
  ...
PASS — 0 stubs found in 47 source files.
```

A failed run produces:

```
$ python3 tools/check_no_stubs.py
Scanning src/jade/ for stubs...
  - tier1_jade/CodeEmitter.cpp:62: FOUND 'TODO: emit prologue'
  - tier1_jade/CodeEmitter.cpp:108: FOUND 'FIXME: handle spill'
FAIL — 2 stubs found. Build blocked.
Add `// NOLINT(no-stubs)` with a WAIVER-NNN reference to bypass.
```
