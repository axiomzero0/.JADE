---
title: "Java / JVM Target Specification"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule A.4", "Rule 42", "Rule 51", "Rule 52"]
pass_type: "Architecture"
tier: "All"
---

# Java / JVM Target Specification

**Status:** Stable  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-19  
**Related Rules:** Rule A.4 (Deopt byte-for-byte match), Rule 42 (Verifier), Rule 51 (Bitmasks), Rule 52 (Correctness-preserving fixes)

---

## 1. What .JADE Compiles

- **Source language**: Java (Java 21+ feature set).
- **Bytecode**: JVM bytecode per JVMS §6.5.
- **Type system**: JVM — primitive types, reference types, **erased generics** (unlike C#'s reified generics), arrays, interfaces, lambdas (via `invokedynamic`).
- **Runtime**: JVM-compatible object model, GC-managed heap, managed pointers, exception model.

`.JADE` is *not* a JVM replacement. It is a JIT compiler that consumes JVM bytecode (as produced by `javac`) and produces native machine code. The runtime services it needs (GC, exception dispatch, class loading) are provided by `.JADE`'s own runtime library, which can either be self-hosted or delegate to a host JVM (Hotspot, OpenJ9, GraalVM) when one is available.

---

## 2. The 4-Tier Pipeline, Adapted to Java

| Tier | Name | Role in a Java context |
| :-- | :-- | :-- |
| T0 | `granit` | **JVM bytecode interpreter**. Reads `.class` files, parses constant pool, executes JVM opcodes on a typed stack. Collects type feedback for receiver classes, branch directions, and `invokevirtual`/`invokeinterface` targets. Polls safepoints at back-edges. |
| T1 | `JADE` | **Baseline SSA JIT**. Lowers JVM bytecode to a flat SSA graph. Fast Linear-Scan register allocation. Emits monomorphic IC stubs for `invokevirtual`/`invokeinterface` (check klass pointer → fast path; miss → vtable/itable lookup). |
| T2 | `RUBY` | **Sea of Nodes optimizing JIT**. Full SoN IR with explicit effect chains. GVN, escape analysis (eliminate non-escaping `new`/`newarray`), LICM (hoist `arraylength`), BCE (for affine loop induction variables), GCM, OSR. |
| T3 | `DIAMOND` | **Peak AOT/JIT hybrid**. PEA for short-lived `ArrayList`/`StringBuilder` that escape only on exception paths. SLP auto-vectorization for adjacent array operations. Speculative devirtualization via CHA. WPD in AOT mode (à la GraalVM Native Image). |

---

## 3. Java-Specific IR Extensions

The following `NodeKind` values are added to support Java-specific semantics. They are defined in `src/jade/ir/NodeKind.hpp`.

### 3.1 Object and Array Operations

| Kind | JVM opcode | Description |
| :-- | :-- | :-- |
| `NewObj`         | `new`            | Allocate a new object. Calls the constructor (treated as a separate `Call`). |
| `NewArr`         | `newarray`/`anewarray`/`multianewarray` | Allocate a new array. |
| `GetField`       | `getfield`       | Load instance field (alias of `LdFld`). |
| `PutField`       | `putfield`       | Store instance field (alias of `StFld`). |
| `GetStatic`      | `getstatic`      | Load static field (alias of `LdFld` with static bit). |
| `PutStatic`      | `putstatic`      | Store static field. |
| `ArrayLength`    | `arraylength`    | Length of an array. |
| `MonitorEnter`   | `monitorenter`   | Acquire object monitor (for `synchronized`). |
| `MonitorExit`    | `monitorexit`    | Release object monitor. |
| `AConstNull`     | `aconst_null`    | Push null (alias of `LdNull`). |

### 3.2 Method Invocation

| Kind | JVM opcode | Description |
| :-- | :-- | :-- |
| `InvokeVirtual`     | `invokevirtual`     | Virtual dispatch via vtable. |
| `InvokeSpecial`     | `invokespecial`      | Direct call (constructors, private, super). |
| `InvokeStatic`      | `invokestatic`       | Static call. |
| `InvokeInterface`   | `invokeinterface`    | Interface dispatch via itable. |
| `InvokeDynamic`     | `invokedynamic`      | Dynamic invocation (lambdas, string concat). Resolves through a bootstrap method. |

### 3.3 Type Checks

| Kind | JVM opcode | Description |
| :-- | :-- | :-- |
| `CheckCast`     | `checkcast`     | Throws `ClassCastException` on failure (alias of `CastClass`). |
| `InstanceOf`     | `instanceof`    | Returns boolean; no throw (alias of `IsInst`). |

### 3.4 Stack Manipulation

JVM has a richer set of stack-manipulation opcodes than CIL. They are modeled as graph rewirings (no node emitted):

| JVM opcode | IR effect |
| :-- | :-- |
| `pop`         | drop top of eval stack |
| `pop2`        | drop top 2 (treating long/double as 1 slot per JVMS) |
| `dup`         | duplicate top |
| `dup_x1`      | duplicate top, insert before second |
| `dup_x2`      | duplicate top, insert before third |
| `dup2`        | duplicate top 2 |
| `dup2_x1`     | duplicate top 2, insert before third |
| `dup2_x2`     | duplicate top 2, insert before fourth |
| `swap`        | swap top 2 |

The lowerer (`src/jade/jvm/Lowerer.cpp`) handles these by manipulating the eval-stack `NodeId` list directly.

---

## 4. JVM Type System

The JVM has 8 primitive types and one reference type at the bytecode level:

| JVM type | Description | `.JADE TypeId` |
| :-- | :-- | :-- |
| `byte`    | 8-bit signed integer | `Int32` (after `i2b` conversion) |
| `short`   | 16-bit signed integer | `Int32` (after `i2s`) |
| `char`    | 16-bit unsigned integer | `Int32` (after `i2c`); stored as `Char` for type narrowing |
| `int`     | 32-bit signed integer | `Int32` |
| `long`    | 64-bit signed integer | `Int64` |
| `float`   | 32-bit IEEE-754 single | `Float32` |
| `double`  | 64-bit IEEE-754 double | `Float64` |
| `boolean` | 1-bit (stored as int 0/1) | `Bool` |
| `reference` | any object reference | `ObjectRef` |

### 4.1 Stack-type collapsing

Per JVMS §2.11.1, the JVM eval stack collapses types:

- `byte`, `short`, `char`, `boolean` → all stored as `int` on the stack.
- `float` and `double` keep their distinct types.

The lowerer tracks the **declared** type (for type narrowing) but the eval-stack representation is `int`.

### 4.2 Erased generics

JVM generics are erased (JVMS §4.6):

- `List<Integer>` and `List<String>` both become `List` at runtime.
- `T` in `class Foo<T>` becomes `Object` (or the bound).
- `T[]` becomes `Object[]`.

This means `invokevirtual List.get(I)Ljava/lang/Object;` always returns `Object`; the caller must `checkcast` to recover the actual type.

`.JADE`'s `invokevirtual` lowering emits a `CheckCast` after the call, matching what `javac` produces. The JIT may speculatively narrow the `CheckCast` based on profile data (Rule 4.2 — speculative devirtualization).

### 4.3 Difference from C# generics

| Aspect | C# (reified) | Java (erased) |
| :-- | :-- | :-- |
| Generic instantiation | Each `List<int>` is a distinct type at runtime. | `List<int>` doesn't exist; only `List<Object>`. |
| Generic method dispatch | Type-based; can devirtualize via type arguments. | Type-erased; receiver type only. |
| `typeof(T)` | Returns the actual runtime type. | Not available (no `T` at runtime). |
| Array covariance | `string[]` is covariant with `object[]` (runtime-checked). | Same; `aastore` performs `checkcast` at runtime. |

The IR is the same; only the lowerer and the cost models differ.

---

## 5. Exception Handling

JVM exceptions are zero-cost in compiled code (Rule 12.3). The IR models exception regions as a side table on `Graph`:

```cpp
struct ExceptionClause {
    uint32_t try_offset;       // start_pc
    uint32_t try_length;       // end_pc - start_pc
    uint32_t handler_offset;   // handler_pc
    uint32_t handler_length;   // unused (JVM doesn't store length)
    ClassId  catch_type;       // catch_type from constant pool; null if 0 (catch-all / finally)
    uint32_t filter_offset;    // 0 (JVM has no filters)
    enum class Kind : uint8_t { Catch, Finally, Fault, Filter };
    Kind kind;                  // Catch for typed catch; Finally for catch-all
};
```

In the IR, `Throw` (`athrow`) is an effectful node that begins the unwind. The verifier (Rule 42) checks that every `Throw` has a `FrameState` so the runtime can reconstruct the JVM stack at the throw site for catch handlers.

### 5.1 `finally` blocks

JVM `finally` is implemented by copying the finally body in the bytecode (`javac` inlines it for every exit path). `.JADE` models this as multiple `Finally` clauses — one per copy.

### 5.2 `synchronized` blocks

JVM `synchronized` blocks compile to `monitorenter`/`monitorexit` pairs, with implicit `monitorexit` in exception handlers. `.JADE` emits `MonitorEnter`/`MonitorExit` nodes and ensures the effect chain calls `MonitorExit` on every exit path, including exceptions.

---

## 6. Profile Feedback for Java Specifically

The `TypeFeedbackVector` (TFV) collected by `granit` tracks, per instruction:

| Field | What it tracks |
| :-- | :-- |
| `invoke_target_seen[]` | Set of `MethodRef` entries observed at `invokevirtual`/`invokeinterface` sites. Drives devirtualization. |
| `klass_seen[]` | For each `checkcast`/`instanceof`, the observed runtime class. Drives spec cast elision. |
| `array_element_type_seen[]` | For each `aaload`/`aastore`, the observed array element class. Drives type specialization. |
| `branch_taken`, `branch_total` | Per-branch direction frequencies. |
| `invocation_count` | Per-method invocation count; drives tier escalation. |
| `loop_back_edge_count` | For OSR triggering. |

---

## 7. Per-Tier Specializations for Java

### 7.1 Tier 0 (`granit`)

- **Receiver class profiling**: Every `invokevirtual`/`invokeinterface` records the receiver's exact klass. Tier 1 emits a monomorphic IC; Tier 3 devirtualizes.
- **`invokedynamic` call site profiling**: Records the resolved CallSite. Tier 3 may inline the bootstrap result.
- **String concatenation**: `invokedynamic` for `makeConcatWithConstants` is profiled; Tier 2/3 may inline the `StringBuilder` chain.

### 7.2 Tier 1 (`JADE`)

- **Monomorphic IC for `invokevirtual`**: Check the receiver's klass pointer; on hit, jump to the known target. On miss, fall back to vtable lookup and deopt.
- **Interface dispatch fast path**: For single-implementation interfaces (CHA), emit a direct call with a `CheckClass` guard.
- **`synchronized` fast path**: For uncontended monitors, use a CAS-based acquire; on contention, fall back to the slow OS call.

### 7.3 Tier 2 (`RUBY`)

- **Escape Analysis for short-lived allocations**: Eliminate `new ArrayList`/`new StringBuilder` that don't escape.
- **BCE for array bounds**: If `i` is an affine induction variable and `0 ≤ i < array.length` is proven at the loop header, eliminate the per-element `aaload` bounds check.
- **LICM for `arraylength`**: Hoist `array.length` out of loops.
- **Inlining of `StringBuilder.append` chains**: Common pattern in Java; inlining unlocks further optimization.

### 7.4 Tier 3 (`DIAMOND`)

- **PEA for `StringBuilder`**: If a `StringBuilder` is built but only escapes on exception paths, PEA delays the allocation to the exception path.
- **PEA for autoboxing**: `Integer.valueOf(int)` boxes an int; PEA eliminates the box on the hot path.
- **SLP for adjacent `aaload`/`aastore`**: Recognize independent same-type array ops and pack them into SIMD instructions.
- **Speculative devirtualization via CHA**: If a virtual method has only one implementation in the loaded class hierarchy, emit a direct call with a `CheckClass` guard.
- **WPD (AOT only)**: In GraalVM Native Image mode, strip the vtable entirely and inline directly across modules.
- **Escape-analyzed `synchronized`**: If `synchronized(this)` is on a non-escaping `this`, elide the monitor entirely (lock elision).

---

## 8. Difference from C# Target

| Aspect | C# | Java |
| :-- | :-- | :-- |
| Generics | Reified | Erased |
| Value types | Yes (`struct`) | No (only primitives) |
| Boxing | Explicit (`box`/`unbox`) | Implicit (autoboxing via `valueOf`) |
| Async | `async`/`await` (state machine) | `CompletableFuture` (no special IR) |
| Delegates | First-class | `FunctionalInterface` + `invokedynamic` |
| Properties | First-class | Convention only |
| `Nullable<T>` | Yes (struct) | `Optional<T>` (reference type) |
| `Vector<T>` | SIMD primitive | `jdk.incubator.vector.Vector` |
| Tail calls | `tail.` prefix | None (JVM doesn't guarantee TCO) |

The IR handles both — every C#-specific node has a Java equivalent, and the lowerer emits the right one based on source language.

---

## 9. What the Existing Foundation Already Supports

The infrastructure built so far supports Java with minimal additions:

- ✅ `BumpAllocator`, `Result<T>`, `Flags<E>`, `NodeId` — language-agnostic.
- ✅ `NodeKind` enum extended with `MonitorEnter`, `MonitorExit` (planned) for `synchronized`.
- ✅ `Graph` + `Verifier` — language-agnostic.
- ✅ `EpochGC`, `SafepointManager` — language-agnostic.
- ✅ `ConstantFolding`, `GVN`, `DCE` — language-agnostic; work on JVM-lowered graphs too.

What's new for Java:

- ➕ `src/jade/jvm/Opcode.{hpp,cpp}` — full JVM opcode table (JVMS §6.5).
- ➕ `src/jade/jvm/Lowerer.{hpp,cpp}` — JVM bytecode → SoN IR lowering.
- ➕ `NodeKind::MonitorEnter` / `MonitorExit` for `synchronized`.
- ➕ Java-specific tests in `tests/jvm/`.
