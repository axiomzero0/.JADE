# Standard Optimization Catalogue

The 12 mechanical categories that run inside `RUBY` (T2) and partially inside `JADE` (T1). The advanced optimizations in `06-optimization-catalog.md` build on top of these.

---

## Tier Assignment Summary

| Tier | Optimizations Active |
| :-- | :-- |
| `granit` (T0) | None (pure interpretation + profiling) |
| `JADE` (T1) | 1.1–1.3, 1.6, 2.1–2.2, 3.1, 3.3, 4.7, 7.3, 9.1, 10.1 |
| `RUBY` (T2) | All of §1–§9, §10, §11.1–11.3, §12. Full GVN, GCM, LICM, BCE, NCE. |
| `DIAMOND` (T3) | All of the above + PEA, SRA, SLP, loop vectorization, speculative devirtualization, PRE, IPO, object coarsening. Aggressive inlining budgets. PBQP regalloc. |

---

## 1. Constant & Algebraic Simplification

The cheapest, highest-frequency passes. Run them first, run them often, run them between every other pass.

| # | Optimization | Mechanical Description |
|---|---|---|
| 1.1 | **Constant Folding** | If all inputs to a pure node are `ConstInt`/`ConstFloat`, evaluate at compile time. `Add(ConstInt(3), ConstInt(4))` → `ConstInt(7)`. Handles `Sub`, `Mul`, `Div`, `Mod`, `Neg`, `Not`, `And`, `Or`, `Xor`, `Shl`, `Shr`, `Eq`, `Ne`, `Lt`, `Gt`, `Lte`, `Gte`. Must handle overflow semantics identically to `granit`. |
| 1.2 | **Algebraic Identity Elimination** | `x + 0 → x`, `x - 0 → x`, `x * 1 → x`, `x / 1 → x`, `x & 0xFFFFFFFF → x` (32-bit mask on 32-bit value), `x | 0 → x`, `x ^ 0 → x`, `x & x → x`, `x | x → x`, `!!x → x` (double negation), `x << 0 → x`, `x >> 0 → x`. |
| 1.3 | **Annihilation** | `x * 0 → 0`, `x & 0 → 0`, `0 / x → 0` (guard: x ≠ 0). Must verify no side effects in `x` before eliminating. |
| 1.4 | **Constant Propagation** | If a `Phi` node has all identical inputs, replace it with that constant. If a `StoreLocal` stores a constant and the next `LoadLocal` reads it, replace the load with the constant. |
| 1.5 | **Copy Propagation** | If `n2 = Copy(n1)`, replace all uses of `n2` with `n1`. Eliminate the `Copy` node. In SoN, this is simply redirecting edges. |
| 1.6 | **Commutative Normalization** | For `Commutative` flagged nodes (`Add`, `Mul`, `Eq`, `And`, `Or`), sort inputs by `NodeId`. This ensures `Add(n5, n3)` and `Add(n3, n5)` hash identically for GVN. |
| 1.7 | **Associative Reassociation** | `(x + 1) + 2` → `x + 3`. `(x * 2) * 3` → `x * 6`. Flatten chains of associative operators and fold constants together. |
| 1.8 | **Distributive Simplification** | `x * a + x * b` → `x * (a + b)`. Only if `a + b` doesn't overflow. Reduces instruction count. |
| 1.9 | **Comparison Folding** | `!(x < y)` → `x >= y`. `!(x == y)` → `x != y`. `x == x` → `true` (if no NaN). `x < x` → `false`. Chained comparisons: `(x < 5) && (x < 10)` → `x < 5`. |
| 1.10 | **Boolean Simplification** | `x && true → x`, `x && false → false`, `x || true → true`, `x || false → x`, `x && x → x`, `x || x → x`, `x && !x → false`, `x || !x → true`. |

---

## 2. Dead Code & Redundancy Elimination

Remove work that produces no observable effect.

| # | Optimization | Mechanical Description |
|---|---|---|
| 2.1 | **Dead Code Elimination (DCE)** | Remove any `Pure` node with zero uses. Iterate until fixpoint. Must respect effect chains: never remove a node with `IsEffect` flag even if it has no data uses (e.g., a `StoreField` to an escaping object). |
| 2.2 | **Unreachable Code Elimination** | After an unconditional `Jump` or `Return`, all subsequent nodes in the same basic block are dead. After `If(ConstBool(true))`, the `IfFalse` branch is dead. Remove dead `Region` inputs; if a `Region` drops to 1 input, replace it with a `Jump`. |
| 2.3 | **Common Subexpression Elimination (CSE)** | Local (within a basic block): if `Add(n3, n4)` appears twice in the same block with no intervening effectful nodes, replace the second with a use of the first. This is the local precursor to GVN. |
| 2.4 | **Redundant Load Elimination** | If `LoadField(obj, offset)` appears twice with no intervening `StoreField(obj, offset, ...)` or `Call` on the effect chain, replace the second load with a data edge to the first. |
| 2.5 | **Redundant Store Elimination** | If `StoreField(obj, offset, v)` is followed by another `StoreField(obj, offset, v2)` with no intervening `LoadField(obj, offset)`, the first store is dead. Remove it. |
| 2.6 | **Redundant Phi Elimination** | If a `Phi` node has all identical inputs, or only one distinct input plus self-loops, replace it with that input. |
| 2.7 | **Redundant Guard Elimination** | If `CheckInt(x)` appears, and `x` is already proven `Int` by a dominating `CheckInt` or by type narrowing, remove the redundant guard. Same for `CheckNotNull`, `CheckShape`, `CheckBounds`. |

---

## 3. Control Flow Simplification

Clean up the CFG/SoN control structure before expensive passes run.

| # | Optimization | Mechanical Description |
|---|---|---|
| 3.1 | **Branch Folding** | `If(ConstBool(true))` → unconditional `Jump` to `IfTrue`. Eliminate the `IfFalse` path. |
| 3.2 | **Branch Inversion** | If the `IfFalse` target is the fall-through successor and `IfTrue` is a jump, invert the condition and swap targets. This improves I-cache locality (fall-through is the hot path per profile). |
| 3.3 | **Empty Block Merging** | If `BlockA` ends with an unconditional `Jump` to `BlockB`, and `BlockB` has exactly one predecessor (`BlockA`), merge them into a single block. Eliminates jump instructions. |
| 3.4 | **Jump Threading** | If `BlockA` ends with `If(cond)` and both `IfTrue`/`IfFalse` targets are unconditional jumps to `BlockC` and `BlockD` respectively, redirect `BlockA`'s branches directly to `BlockC`/`BlockD`, eliminating the intermediate blocks. |
| 3.5 | **Tail Merging (Code Sinking)** | If multiple predecessor blocks end with identical instruction sequences (e.g., the same epilogue), merge those tails into a single shared block. Reduces code size. |
| 3.6 | **Switch-to-If Lowering** | If a `Switch` has only 2 cases, lower it to an `If`. If it has 3-4 sparse cases, lower to a chain of `If`/`Eq` comparisons. Dense tables remain as jump tables. |
| 3.7 | **If-to-Switch Raising** | If a chain of `If(x == 1) ... If(x == 2) ... If(x == 3)` is detected, raise it to a single `Switch` node. Enables jump-table emission in `asmjit`. |
| 3.8 | **Loop Rotation** | Transform a `while` loop (test at top) into a `do-while` loop (test at bottom) with a pre-header guard. This eliminates one branch per iteration and exposes the loop body to LICM. |
| 3.9 | **Critical Edge Splitting** | If an edge goes from a block with multiple successors to a block with multiple predecessors, insert an empty `Jump` block on that edge. Required before SSA construction and register allocation to correctly place `Phi` nodes and avoid breaking live ranges. |

---

## 4. Strength Reduction & Instruction Selection

Replace expensive operations with cheaper equivalents.

| # | Optimization | Mechanical Description |
|---|---|---|
| 4.1 | **Multiply-to-Shift** | `x * 2` → `x << 1`, `x * 4` → `x << 2`, `x * 8` → `x << 3`. General: `x * 2^k` → `x << k`. |
| 4.2 | **Divide-to-Shift** | `x / 2` → arithmetic right shift with bias (for signed), `x / 4` → `x >> 2`. Unsigned: simple logical shift. Must handle negative dividends correctly. |
| 4.3 | **Modulo-to-AND** | `x % 2^k` → `x & (2^k - 1)` for unsigned. For signed: more complex bias correction. |
| 4.4 | **Multiply-to-LEA** | `x * 3` → `x + (x << 1)`. `x * 5` → `x + (x << 2)`. `x * 9` → `x + (x << 3)`. Maps directly to x86-64 `LEA` instruction. |
| 4.5 | **Address Computation Folding** | `base + (index * scale) + displacement` → single `LEA` or memory operand `[base + index*scale + disp]`. Fold during lowering to `asmjit`. |
| 4.6 | **Negation Folding** | `0 - x` → `Neg(x)`. `x - y` where `y` is constant → `x + (-y)`. Avoids a register for the zero. |
| 4.7 | **Compare-and-Branch Fusion** | `Cmp(x, y)` immediately followed by `If(Eq/Ne/Lt/Gt)` → fused `cmp` + `jcc` in x86-64. No intermediate flag register needed. |
| 4.8 | **Conditional Move (CMOV) Selection** | If a branch is short and unpredictable (profile: ~50% taken), replace `If(cond) a else b` with `CMOV`. Eliminates branch misprediction penalty. Guarded by cost model (Rule 47): only if both paths are side-effect-free and cheap. |
| 4.9 | **Integer-to-Float Fusion** | `ToFloat(Add(ConstInt(a), ConstInt(b)))` → `ConstFloat(a + b)`. Don't do the integer add then convert; fold the whole thing. |

---

## 5. Loop Optimizations (Standard)

The workhorses for numerical/array-heavy code.

| # | Optimization | Mechanical Description |
|---|---|---|
| 5.1 | **Loop Invariant Code Motion (LICM)** | For every pure node inside a `Loop` region: if all its data inputs are defined outside the loop (or are loop-invariant), and it has no effect dependencies inside the loop, hoist it to the loop pre-header. |
| 5.2 | **Induction Variable Elimination** | Identify loop induction variables (`i = i + 1`). Replace derived values (`i * 4`, `base + i * stride`) with a single pointer increment. Eliminates the multiply from the loop body. |
| 5.3 | **Loop Unrolling (Small Factor)** | Unroll hot loops by 2× or 4×. Reduces branch overhead, exposes ILP, enables SLP vectorization. **Rule 47**: Only if profile shows high iteration count and the loop body is small. Budget: max 8× unroll. |
| 5.4 | **Loop Peeling** | Peel the first N iterations (typically 1) out of the loop. Enables specialization of the first iteration (e.g., no bounds check needed if length ≥ 1 is proven). |
| 5.5 | **Loop Unswitching** | If a loop contains a conditional that is loop-invariant (`if (flag)` where `flag` doesn't change in the loop), hoist the condition outside, duplicating the loop. Two simpler loops are easier to optimize than one complex loop. |
| 5.6 | **Loop Exit Test Simplification** | If the loop exit condition can be proven from the induction variable range, simplify or eliminate redundant exit checks. |
| 5.7 | **Empty Loop Elimination** | If a loop body has no side effects and no observable output, eliminate the entire loop. `for (i = 0; i < n; i++) {}` → nothing. |

---

## 6. Function-Level Optimizations

| # | Optimization | Mechanical Description |
|---|---|---|
| 6.1 | **Function Inlining** | Replace a `Call` node with the callee's body (as SoN subgraph). **Rule 47**: Inline only if callee size < budget (e.g., 35 nodes), call site is hot (profile), and the call is monomorphic. Recursive calls: inline max 1 level. |
| 6.2 | **Tail Call Elimination (TCE)** | If a `Call` is immediately followed by `Return` with no intervening effectful operations, reuse the current frame. Emit a `Jump` instead of `Call` + `Ret`. |
| 6.3 | **Argument Specialization** | If a function is always called with a constant argument (e.g., `array.get(0)`), clone the function with that argument replaced by the constant. Enables further constant folding inside the clone. |
| 6.4 | **Return Value Forwarding** | If a `Call` returns a value that is immediately stored and then the caller returns, forward the return value directly. Eliminates the temporary. |
| 6.5 | **Dead Parameter Elimination** | If a function parameter is never used in the body, remove it from the signature. Update all call sites. Reduces register pressure and calling convention overhead. |
| 6.6 | **Callee-Saved Register Minimization** | During lowering, if the inlined function body doesn't use certain callee-saved registers, don't emit push/pop for them in the prologue/epilogue. |

---

## 7. Type & Shape Specialization

Dynamic language specific. Leverages `granit`/`JADE` profiles.

| # | Optimization | Mechanical Description |
|---|---|---|
| 7.1 | **Type Narrowing (Speculative)** | If TFV says a value is always `Int`, insert a `CheckInt` guard at the definition, then propagate `TypeId::Int` through the graph. All downstream `Add` nodes become integer-only (no float promotion check). |
| 7.2 | **Shape Guard Specialization** | For `LoadField(obj, "x")`: if profile shows `obj` always has Shape A, emit `CheckShape(obj, ShapeA)` then a direct memory loading at the known offset. No hash-table lookup. |
| 7.3 | **Monomorphic IC Stub Emission** | At `JADE` tier: for a property access with one observed shape, emit: `cmp [obj + shape_offset], expected_shape` → `jne deopt` → `mov result, [obj + field_offset]`. Single branch, single load. |
| 7.4 | **Polymorphic IC Chain** | For 2-3 observed shapes: emit a chain of shape checks. Each check is a bitmask compare (Rule 51). Falls through to the matching offset load. Miss → runtime C++ stub. |
| 7.5 | **Megamorphic Fallback** | If >3 shapes observed: stop specializing. Emit a single call to the runtime property lookup. Don't waste code cache on long IC chains. |
| 7.6 | **Integer/Float Fast-Path Splitting** | For arithmetic: if profile says 99% `Int`, emit the integer fast path inline. On overflow or type mismatch, jump to a cold out-of-line float promotion stub. The hot path never checks for float. |

---

## 8. Memory & Pointer Optimizations

| # | Optimization | Mechanical Description |
|---|---|---|
| 8.1 | **Load Hoisting** | If a `LoadField` is executed on every path through a region but the object reference is the same, hoist the load above the region. |
| 8.2 | **Store Sinking** | If a `StoreField` is executed on only one path but the value is available on all paths, sink the store to the latest possible point (reduces register pressure). |
| 8.3 | **Allocation Sinking** | If an `Allocate` is immediately followed by field stores and the object doesn't escape the current block, sink the allocation to the latest point before the first use. Reduces live range. |
| 8.4 | **Write Barrier Elision** | If the GC write barrier is only needed for old→young references, and the object is provably young (just allocated in this function), eliminate the write barrier on stores to its fields. |
| 8.5 | **Array Length Hoisting** | Hoist `LoadField(array, "length")` out of loops. The length of an array doesn't change during iteration (unless a mutating call occurs, which the effect chain tracks). |

---

## 9. Register Allocation & Spilling (Standard)

| # | Optimization | Mechanical Description |
|---|---|---|
| 9.1 | **Linear Scan Register Allocation** | Wimmer-Franz algorithm. O(n log n). Assign virtual registers to physical registers. Spill the least-profitable live ranges. Used in `JADE` (Tier 1). |
| 9.2 | **Live Range Splitting** | If a value is live across a call (which clobbers caller-saved registers), split the live range at the call. Spill before, reload after. Avoids spilling the entire range. |
| 9.3 | **Rematerialization over Spilling** | If a value is trivially recomputable (constant, `Add` of two live registers), recompute it instead of spilling to the stack. Saves a `mov` + memory access. |
| 9.4 | **Coalescing** | If a `Copy`/`Phi` connects two virtual registers, attempt to assign them the same physical register. Eliminates the `mov` instruction entirely. |
| 9.5 | **Callee-Saved vs Caller-Saved Selection** | If a register is used across a call, prefer callee-saved registers (push/pop once in prologue/epilogue) over spilling to the stack at every call site. |

---

## 10. Peephole & Post-Regalloc Optimizations (`asmjit` level)

These run on the linear instruction stream *after* register allocation, immediately before `asmjit` emits bytes.

| # | Optimization | Mechanical Description |
|---|---|---|
| 10.1 | **Redundant Move Elimination** | `mov rax, rbx` followed by `mov rcx, rax` → `mov rcx, rbx`. `mov rax, rax` → delete. |
| 10.2 | **Immediate Folding** | `mov rax, 5` + `add rax, rbx` → `lea rax, [rbx + 5]`. One instruction instead of two. |
| 10.3 | **Address Mode Folding** | `mov rax, rbx` + `mov rcx, [rax + 8]` → `mov rcx, [rbx + 8]`. Eliminates the intermediate register. |
| 10.4 | **Shift-Add to LEA** | `shl rax, 3` + `add rax, rbx` → `lea rax, [rbx + rax*8]`. Single instruction. |
| 10.5 | **Test-and-Branch Fusion** | `and rax, rax` + `jz label` → `test rax, rax` + `jz label`. Or better: if the previous instruction already sets flags (e.g., `sub`), eliminate the `test` entirely. |
| 10.6 | **Sign/Zero Extension Elision** | If a 32-bit operation already zero-extends to 64 bits (x86-64 semantics), eliminate a subsequent `movzx` or `cdqe`. |
| 10.7 | **Stack Frame Shrinking** | After regalloc, count the actual number of spill slots used. Shrink the `sub rsp, N` in the prologue to the minimum required. Reduces stack pressure and cache footprint. |
| 10.8 | **NOP Sled Removal** | Remove any alignment padding NOPs that are no longer needed after code size changes. |

---

## 11. Profiling & Feedback-Directed (Standard)

| # | Optimization | Mechanical Description |
|---|---|---|
| 11.1 | **Hot/Cold Code Splitting** | Place hot basic blocks (top 90% of execution frequency) contiguously in memory. Cold blocks (error paths, deopt stubs, rare branches) go to a separate cold section. Maximizes I-cache utilization. |
| 11.2 | **Profile-Guided Inlining Order** | Inline the hottest callees first. If inlining A exposes that B is now hot, inline B next. BFS over the call graph weighted by profile frequency. |
| 11.3 | **Branch Prediction Hint Emission** | For `asmjit`: emit `likely`/`unlikely` prefixes (x86-64: `jcc` with `0x2E`/`0x3E` segment override prefixes, or rely on fall-through convention). Layout hot path as fall-through. |
| 11.4 | **Invocation Counter Threshold Tuning** | `granit` → `JADE` promotion at N invocations (e.g., 100). `JADE` → `RUBY` at M (e.g., 1000). `RUBY` → `DIAMOND` at K (e.g., 10000) + stability check. Prevents compiling cold code. |
| 11.5 | **Back-Edge Counter for OSR** | Count loop iterations. If a single invocation of a function loops >10000 times, trigger OSR compilation without waiting for the function to return. |

---

## 12. Exception & Safety Optimizations

| # | Optimization | Mechanical Description |
|---|---|---|
| 12.1 | **Try-Catch Sinking** | If a `TryBegin`/`TryEnd` region contains no operations that can throw (all nodes have `NoThrow` flag), eliminate the try-catch entirely. |
| 12.2 | **Exception Table Compression** | Merge adjacent bytecode ranges that map to the same catch handler. Reduces exception table size and lookup time. |
| 12.3 | **Zero-Cost Exception Model** | No runtime cost on the non-exceptional path. Exception metadata lives in a side table (`.eh_frame`-style). The hot path has zero branches for exception checking. |
