# Non-Bypassable Performance & Correctness Laws

These are the law. Break them, and .JADE will be slow, incorrect, or unmaintainable.

---

## A. Runtime & Speculation Laws

### A.1 Never optimize dynamic behavior without profile data
.JADE is dynamic. You cannot statically know enough. Tier 0/1 **must** collect profiles; Tier 2/3 **must** consume them. No speculative optimization is allowed without proof, or `(profile + guard + deopt path)`.

### A.2 Every speculative optimization must have a bailout path
If you assume a value is `Int`, an array index is in bounds, or a type is monomorphic, you must emit a `guard -> success path -> deopt/failure path`. **No guard, no speculation.**

### A.3 Every guard must have a reconstructible deopt state
A guard without a reconstructible runtime state is useless. For every failing guard, the JIT must know:
- the baseline register state,
- the stack state,
- the locals state,
- the bytecode offset,
- and the pending exception state.

This is captured in a `FrameState` node attached to every potentially-failing node.

### A.4 Deoptimization must produce the exact same observable behavior as `granit`
Non-negotiable. If the JIT optimizes `x + y` to a hardware `add`, a deopt on overflow must restore the exact value `granit` would have computed, including slow-path promotions (e.g., integer overflow → promote to BigInt, or float, per `.JADE` semantics).

### A.5 Profiling must be conservative
Profile data is a hint, not a proof. Profiles lie (new types appear, shapes transition). The JIT must always be prepared to deopt and recompile with new data.

---

## B. Compilation Pipeline Laws

### B.1 No `malloc`/`free` in the JIT hot path
Use arena/bump allocators. Every `Node`, `BasicBlock`, and side-table entry is allocated from a thread-local arena bulk-freed at the end of compilation. `malloc` destroys throughput.

### B.2 No exceptions in the JIT hot path
Compile the JIT proper with `-fno-exceptions`. Use `std::expected` / `Result<T>` for fallible operations. Compiler errors are recoverable: abort compilation, fall back to a lower tier, keep running.

### B.3 No RTTI in the JIT hot path
Compile with `-fno-rtti`. Use `enum class NodeKind` and `switch` statements. RTTI costs cycles and disables devirtualization.

### B.4 No `std::shared_ptr` / `std::function` in hot IR mutation code
They allocate. Use raw pointers + stable `NodeId`s inside passes.

### B.5 Every pass must be idempotent
Running the same pass twice must produce the identical IR. Otherwise, fixpoint iteration will loop forever.

### B.6 Every pass must be monotonic decreasing in IR size
A pass either reduces node count or moves the IR closer to a normal form. If a pass can grow the IR (e.g., loop unrolling), it must run inside a guarded fixpoint with a strict budget.

---

## C. Memory & Threading Laws

### C.1 Mutator threads must never block on JIT compilation
If a mutator needs compiled code that isn't ready, it falls back to `granit`. It does not wait.

### C.2 Mutator threads must never block on GC locks
Thread-local bump arenas for allocation. Global GC synchronization happens *only* at safepoints.

### C.3 Compiler threads must never block on mutator state
The compiler works on a frozen snapshot of the IR. Mutator updates after the snapshot are picked up by the next compilation.

### C.4 Memory reclamation must be epoch-based, not lock-based
When the optimizer replaces a `Node`, the old node is tagged with an epoch. Once all compiler threads advance past that epoch, the memory is bulk-freed. This avoids both locks and use-after-free.

---

## The Numbered Rules (36–52)

Compiler bugs are uniquely expensive because they corrupt trust. Users who get wrong results silently will never come back. These rules make entire categories of bugs impossible.

### Rule 36 — Five regression tests per bug fix
A fix is not enough. Every bug fix must include at least **5 regression tests** covering distinct views of the failure:

1. **Minimal reproducer** — smallest possible input that triggers the bug.
2. **Variant trigger** — different code pattern exercising the same root cause.
3. **Boundary/negative** — ensures the fix doesn't over-correct.
4. **Integration/contextual** — the bug in realistic surrounding code.
5. **Deopt/state reconstruction** — verifies correctness under bailout.

**Enforcement:** CI fails if a PR labeled `bugfix` has fewer than 5 new test cases. No exceptions.

### Rule 37 — Golden tests for every pass
Every optimization pass must have ≥10 golden IR tests before merging. Checked-in `.in.ir` / `.out.ir` file pairs.

### Rule 38 — Differential testing is mandatory in CI
`granit` ↔ `JADE` ↔ `RUBY` ↔ `DIAMOND` comparisons run on every PR. Divergence blocks merge. Assert byte-for-byte identical results.

### Rule 39 — Deopt paths must be fuzzed weekly
Scheduled CI job. Results triaged within 24 hours. Untriaged deopt fuzz failures block releases.

### Rule 40 — Replay logs retained for all CI failures
Failed test runs automatically save full compile replay artifacts (bytecode + profile + options + RNG seed). Debugging starts from replay, not reproduction.

### Rule 41 — Performance regressions require explicit waiver
If a benchmark regresses >5%, the PR must include root cause analysis, justification, a tracking issue, and approval. No silent performance degradation.

### Rule 42 — Graph verifier runs in debug builds after *every* pass
Not optional. The verifier checks:
- no dangling `NodeId`s,
- effect chain continuity,
- control dominance,
- use-def consistency,
- no dead nodes with live users,
- `FrameState` attached to every guard.

### Rule 43 — Test names encode the bug/feature they cover
Bad: `test_pea_3`. Good: `pea_non_escaping_object_with_deopt_materializes_correctly`. Searchable, self-documenting.

### Rule 44 — No assumption without invalidation
Every speculative assumption must have a registry entry (Watchdog), an invalidation path (Trip), dependent code tracking, and a fallback tier.

### Rule 45 — No specialization without fallback
Every specialized clone must have a generic fallback, a deopt path, a profile downgrade path, and a budget limit.

### Rule 46 — No profile data without confidence
Profile data must include sample count, stability, age, decay, variance, and deopt correlation (Meter). Low-confidence data must not trigger aggressive speculation.

### Rule 47 — No aggressive pass without a cost model
Inlining, cloning, unrolling, vectorization, block duplication, loop versioning, and PEA materialization must all use a strict cost model (Regulator).

### Rule 48 — No FFI optimization without ABI proof
FFI optimizations must prove calling convention correctness, stack alignment, register clobbering, thread state transition, and memory ownership.

### Rule 49 — No vectorization without dependence proof
Vectorization must prove no aliasing (or versioned check), bounds safety, alignment, correct remainder handling, and correct scalar fallback.

### Rule 50 — No persistent state without versioning
Profile caches, code caches, serialized snapshots, AOT artifacts, and compiled metadata must all be versioned.

### Rule 51 — All orthogonal boolean state must be bitmasked
Any set of independent boolean properties on a hot-path data structure (e.g., `NodeFlags`) must be represented as a bitmask with type-safe `Flags<E>` wrappers. Raw integers are forbidden for flag-like state. All bitmask types must have:
- symbolic printing,
- debugger visualizers,
- compile-time validation.

### Rule 52 — No easy fixes — only correctness-preserving performance fixes
When fixing a bug, performance regression, or correctness issue, you must implement the fix that *simultaneously*:
- preserves or improves runtime performance,
- maintains full semantic correctness.

"Easy" fixes that sacrifice either property are forbidden unless explicitly documented as temporary mitigations with tracking issues and removal deadlines. Every fix must include:
1. **Proof of correctness**
2. **Performance validation**
3. **Explanation of why easier alternatives were rejected**
