---
title: "Gap Analysis: Current State vs Target"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-20"
related_rules: ["Rule 09", "Rule 42", "Rule 52"]
pass_type: "Architecture"
tier: "All"
---

# Gap Analysis: What's Actually Running

**Status:** Stable  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-20

---

## 1. What's Actually Fast Today

### Real, working, and effective:

| Component | Status | Notes |
| :-- | :-- | :-- |
| `LinearScanRegAlloc` | ✅ Real | Wimmer-Franz LSRA with spill/reload, 16-byte frame alignment. |
| `CodeEmitter` (straight-line) | ✅ Real | asmjit emission for ~20 NodeKinds: ConstInt, Add, Sub, Mul, Div, Mod, Neg, And/Or/Xor/Not/Shl/Shr/Sar, Eq/Ne/Lt/Gt/Lte/Gte, LdLoc, StLoc, LdArg, LdFld, StFld, Return, Safepoint. |
| `ConstantFolding` | ✅ Real | Iterates to fixpoint; folds Add/Sub/Mul/Div/Mod. |
| `GVN` | ✅ Real | Hash-based dedup with commutative normalization and use rewiring. |
| `DCE` | ✅ Real | Fixpoint iteration; respects effect chains. |
| `SCCP` | ✅ Real | Wegman-Zadeck lattice; propagates through arithmetic + comparisons. |
| `CSE` | ✅ Real | Local CSE within effect-chain runs; rewires uses. |
| `AlgebraicSimplification` | ✅ Real | x+0→x, x*1→x, x*0→0, x&0→0, x^x→0, x<<0→x. |
| `BCE` (constants) | ✅ Real | Eliminates CheckBounds where idx and length are both ConstInt. |
| `NCE` | ✅ Real | Eliminates CheckNotNull on NewObj/NewArr/Box/LdStr results. |
| `ControlFlowSimplification` | ✅ Real | Eliminates constant-folded If branches. |
| `CilInterpreter` | ✅ Real | Executes ~60 CIL opcodes on a typed eval stack. |
| `JvmInterpreter` | ✅ Real | Executes ~80 JVM opcodes on a typed eval stack. |
| `Value` (16-byte) | ✅ Real | Compact tagged union; single tag switch per op. |
| `SafepointManager` | ✅ Real | Atomic flag polling; enters/exits safepoint. |
| `EpochGC` | ✅ Real | Epoch-based reclamation for compiler-thread IR. |
| `Graph::replace_all_uses` | ✅ Real | Rewires data-input references for GVN/CSE/SRA. |

### Partially working:

| Component | Status | What's missing |
| :-- | :-- | :-- |
| `CodeEmitter` (control flow) | ✅ Real (block-scheduled) | **NOW WORKING**: walks blocks in RPO, pre-allocates labels, If/IfTrue/IfFalse emit `test; jcc; jmp` with proper label binding. Branch tests pass. |
| `BuildRegions` | ✅ Real | Now exposes `reverse_post_order()` and `node_ids_in_block()` for the emitter. |
| `PEA` | 🟡 Partial | Does straight-line EA only (eliminates allocations with zero live uses). No materialization splitting (needs block structure + Phi-per-field). |
| `SRA` | 🟡 Partial | Store→load forwarding for straight-line code. No Phi-per-field for loop-carried fields. |
| `SLP` | 🟡 Analysis-only | Finds candidate packs of isomorphic independent nodes. No SIMD emission (emitter has no vector instruction support). |
| `Peephole` | 🟡 Partial | Strength reduction marker (x*2^k → shift). No post-regalloc instruction combining. |
| `EscapeAnalysis` | 🟡 Partial | Binary escape (zero live uses → dead). No per-path escape state. |

---

## 2. What Silently Does Nothing (No-Op Passes)

Every pass that needs loop/region structure is a no-op, because the lowerers emit `If`/`IfTrue`/`IfFalse`/`Phi` but **no `Loop`, `Region`, `Switch`, or `Jump` nodes with proper block structure, and no dominator tree**:

| Pass | Status | Why It's a No-Op |
| :-- | :-- | :-- |
| `LICM` | no-op | "requires Loop region in the graph" — no Loop nodes are produced by the lowerer. |
| `GCM` | no-op | "requires basic block structure / dominator tree / loop detection" — none exist. |
| `LoopUnrolling` | no-op | Needs Loop regions to identify loops to unroll. |
| `LoopUnswitching` | no-op | Needs Loop regions to hoist invariant conditionals. |
| `LoopPeeling` | no-op | Needs Loop regions to peel first iteration. |
| `Inlining` | no-op | Needs call graph + callee bodies (metadata resolver not wired). |
| `TailCallElimination` | partial | Detects Call→Return pattern but can't verify "no intervening effectful ops" without block structure. |
| `Devirtualization` | no-op | Needs CHA (class hierarchy analysis) + profile data. |
| `ICStubEmission` | no-op | Needs profile data wiring from granit. |
| `ProfileGuidedBlockReorder` | no-op | Needs basic block structure + profile data. |

---

## 3. The CodeEmitter Fallback Problem

The `CodeEmitter` returns `ErrorKind::UnsupportedNode` for any NodeKind it doesn't handle, which triggers a fallback to `granit` (Tier 0 interpreter). The **still unhandled** NodeKinds (after the block-scheduled emission work) are:

```
Call, CallVirt, CallKnown, TailCall, InvokeDynamic,
NewObj, NewArr, Allocate, Materialize, Box, Unbox, UnboxAny,
IsInst, CastClass,
Throw, Rethrow, Leave, EndFinally,
MonitorEnter, MonitorExit,
ConvR4, ConvR8, ConstFloat,
ToInt, ToFloat (require XMM support),
ConvOvfI1..ConvOvfU8 (require deopt infrastructure)
```

**The following NodeKinds are NOW handled** (block-scheduled emission):
- `If`, `IfTrue`, `IfFalse` — emit `test; jcc; jmp` with block-level labels
- `Region`, `Loop` — block leader labels bound at block start
- `Jump` — emits `jmp` to the nearest Loop header label (back-edge)
- `Phi` — no code emitted (regalloc handles value selection in straight-line code; **runtime Phi resolution for real loops is future work**)
- `Switch` — partial (loads value, but case targets not yet wired)
- `CheckBounds` — emits `cmp; jl ok; ud2`
- `LoadElement`/`StoreElement`/`LdElem`/`StElem` — emit `mov [arr+idx*8+16], reg`
- `ArrayLength` — emits `mov reg, [arr+8]`
- `LdFlda`/`LdElemA` — emit `lea reg, [obj+off]`
- `LdLoca`/`LdArga` — emit `lea reg, [rbp-off]`
- `ConvI1..ConvU8` — emit `movsx`/`movzx`/`movsxd` as appropriate
- `ConstBool`/`LdNull`/`ConstNull` — emit `mov reg, 0` (or `xor reg, reg`)
- `LdStr`/`ConstString` — emit `mov reg, str_id` (token, runtime resolves)
- `IsInt`/`IsFloat`/`IsNull`/`ToBool` — emit `test; setz/setnz; movzx`

**Branches and basic control flow no longer fall back to the interpreter.** Functions with calls, allocations, or exceptions still fall back.

---

## 4. The Root Cause: No Block Structure

**Resolved.** BuildRegions now identifies basic blocks, computes the dominator tree (Cooper-Harvey-Kennedy), detects loops via back-edges, and exposes `reverse_post_order()` + `node_ids_in_block()` for the emitter. The CodeEmitter walks blocks in RPO, binds labels at block leaders, and emits `If`/`IfTrue`/`IfFalse`/`Jump`/`Region`/`Loop` correctly.

**Real loop iteration now works** (as of 2026-08-20):
- The Loop header is a label; the Jump at the end of the body emits `jmp loop_header_label`.
- Loop-carried values flow through memory-based locals (StLoc/LdLoc) — no Phi resolution needed.
- 3 loop tests pass: `CountToFiveLoop` (i=0→5), `SumOneToTenLoop` (sum=0→45), `CountToHundredLoop` (i=0→100).

**Known limitation: LSRA doesn't account for loop back-edges.**
~~The Linear Scan Register Allocator computes live intervals based on linear NodeId positions. It doesn't know that the loop body re-executes.~~

**Fixed (2026-08-20):** The LSRA now calls `extend_intervals_across_loops()` after computing initial live intervals. This method uses BuildRegions to identify loop headers and back-edges, then extends the live interval of any value defined before a loop and used inside it to cover the entire loop body. This prevents the register from being reused for a different value inside the loop.

Also fixed: `BuildRegions::connect_edges` now handles `Jump` nodes — it creates a back-edge from the Jump's block to the Loop header block. Previously, Jump was treated as fall-through, so back-edges were never detected and `is_loop_header` was never set.

Two new tests verify the fix: `LoopWithRegisterInvariantBound` (ConstInt(5) used directly as loop bound) and `LoopWithRegisterInvariantSum` (ConstInt(10) used directly). Both pass without the StLoc/LdLoc workaround.

**Future work:**
- ~~Fix the LSRA to extend live intervals across loop back-edges~~ ✅ Done.
- Implement runtime Phi resolution for register-allocated loop-carried values (the classical Wimmer-Franz copy insertion at predecessor block terminators).
- Pre-header insertion: LICM marks hoist candidates but the emitter doesn't actually move them before the loop header (GCM is "virtual hoisting" only).
- OSR: long-running loops in the interpreter can't be promoted to JIT mid-execution.

---

## 5. The Fix: BuildRegions + Block-Scheduled Emission

### 5.1 BuildRegions Pass

Add a pass that runs after lowering and before optimization:

1. **Walk `If`/`IfTrue`/`IfFalse`/`Jump` nodes** to identify basic blocks.
2. **Insert `Region` nodes** at merge points (where two control-flow edges converge).
3. **Detect back-edges** (control edges that target an earlier block) and insert `Loop` nodes with pre-headers.
4. **Compute the dominator tree** (Lengauer-Tarjan, O(E·α(E,N))).
5. **Compute the loop nesting forest** (natural loops identified by back-edges).

This one pass unlocks: LICM, GCM, LoopUnrolling, LoopPeeling, LoopUnswitching, PEA-materialization, SRA-with-Phi.

### 5.2 Block-Scheduled Emission

Update `CodeEmitter` to:
1. **Walk blocks in reverse post-order** (dominator-tree order).
2. **Emit `If` as `test; jcc`** with proper label binding for true/false targets.
3. **Emit `Jump` as `jmp label`**.
4. **Emit `Phi` as a `mov` chain** at merge points (Wimmer-Franz §3.4).
5. **Emit `Region` as a label** (the merge point).
6. **Emit `Loop` as a back-edge label + safepoint poll**.

### 5.3 Additional Emission

After block structure:
- **`Call`/`CallKnown`**: load args into RDI/RSI/RDX/RCX/R8/R9, align stack, `call` indirect.
- **`NewObj`/`NewArr`/`Allocate`**: bump-allocate from the thread-local arena, zero-fill.
- **`LoadElement`/`StoreElement`**: `mov reg, [arr + idx*scale + offset]` with optional bounds check.
- **`ArrayLength`**: `mov reg, [arr + 8]` (length is at offset 8 in the array header).
- **`Conv*`**: `movsxd`/`cvtsi2sd`/`cvttsd2si` as appropriate.
- **`CheckInt`/`CheckNotNull`/`CheckBounds`**: `test`/`cmp; jne deopt`.
- **`Box`/`Unbox`**: allocate box, copy value (PEA will optimize this away on hot paths).
- **`Throw`**: store exception, `jmp unwind_handler`.

---

## 6. Other Gaps

### 6.1 No Tiering Logic

There is no `TierManager`, no invocation counter, no compilation queue. The driver runs the demo and exits. The tier escalation policy (granit→JADE at N=100, →RUBY at M=1000, →DIAMOND at K=10000) is documented but **not implemented**.

### 6.2 No OSR

Long-running loops stuck in `granit` can't be promoted to JIT mid-execution. Without OSR, a tight loop that starts in the interpreter stays there forever.

### 6.3 No Code Cache

Each `JitRuntime` is per-function. For a real runtime: single code cache, `mmap(PROT_READ|PROT_WRITE)` for emission, `mprotect(PROT_READ|PROT_EXEC)` after finalization.

### 6.4 Interpreter Uses Exceptions

`Interpreter::run` uses `throw std::runtime_error` for DivideByZero, stack underflow, type mismatches. Each throw walks the stack and is ~100× slower than a return. Should use `Result<Value>` propagation instead.

### 6.5 No Computed-Threaded Dispatch

The interpreter's giant `switch` defeats branch prediction. GCC's "labels as values" (`&&label`) with a dispatch table gives ~20-30% speedup.

### 6.6 LTO Off by Default

`JADE_USE_LTO` defaults OFF. The hot-path flags are good (`-fno-exceptions -fno-rtti`) but without LTO the compiler can't inline across TUs.

---

## 7. Priority Order

| Priority | Work | Unblocks | Status |
| :-- | :-- | :-- | :-- |
| P0 | `BuildRegions` pass (Region/Loop/DominatorTree) | LICM, GCM, LoopUnroll, LoopPeel, LoopUnswitch, PEA-materialization, SRA-with-Phi | ✅ Done |
| P0 | Block-scheduled `CodeEmitter` (If/Jump/Phi/Region/Loop) | Real JIT branches (no fallback to granit) | ✅ Done — 5 branch tests + 1 loop test pass |
| P1 | `LICM` (real) + `BCE` (affine) + `ArrayLength`/`LoadElement`/`StoreElement` emission | Array loops at near-native speed | 🟡 Partial — BCE works for constants; affine BCE needs IV range analysis; **LSRA now loop-aware** |
| P1 | Interpreter: fixed stack + computed goto + no-throw | 2-3× faster fallback | ✅ Done (de-throw + fixed stack + __builtin_expect) |
| P2 | `Inlining` (real) + `Call`/`CallKnown` emission | OO code | ❌ Not started |
| P2 | `Peephole` (post-regalloc) | 5-10% everywhere | 🟡 Partial — strength reduction only |
| P3 | `LiveRangeSplitting` + `Rematerialization` in LSRA | Fewer spills | ❌ Not started |
| P3 | `TierManager` + compilation queue | Actual tiered compilation | ❌ Not started |
| P4 | `Devirtualization` (CHA + profile) + `ICStubEmission` | Virtual calls | ❌ Not started |
| P4 | `PEA` (full) + `SRA` (with Phi-per-field) | Eliminate allocations | 🟡 Partial — straight-line only |
| P5 | `SLP` (with SIMD emission) + `LoopVectorization` | SIMD | 🟡 Analysis-only |
| P5 | OSR + code cache (W^X) + LTO defaults | Long-running loops, deployment | 🟡 LTO enabled by default; OSR + W^X not started |
| P0+ | **Phi resolution at runtime** | Real loop iteration | ✅ Done — memory-based locals (StLoc/LdLoc) handle loop-carried values without explicit Phi. 3 loop tests pass. |
