# .JADE

A 4-tier, profile-driven, speculation-heavy JIT compiler for **both C# and Java**, written in C++23.

> **Status:** Early development. The compiler consumes **full** CIL (ECMA-335) and **full** JVM (JVMS §6.5) bytecode — not a subset. The same Sea-of-Nodes IR, the same optimization passes, and the same backend serve both source languages.

## The 4-Tier Pipeline

The pipeline is target-agnostic. CIL and JVM opcodes lower to the same `NodeKind`s.

```
              ┌──────────────┐
   CIL ─────► │              │
   (.dll)     │   granit    │  Tier 0 — Interpreter
              │  (T0, base)  │  Zero compilation latency, profile collection
   JVM ─────► │              │
   (.class)   └──────┬───────┘
                     │ after N invocations
                     ▼
              ┌──────────────┐
              │    JADE      │  Tier 1 — Baseline SSA JIT
              │   (T1)       │  Eliminate dispatch overhead in milliseconds
              └──────┬───────┘
                     │ after M invocations
                     ▼
              ┌──────────────┐
              │    RUBY      │  Tier 2 — Sea of Nodes Optimizing JIT
              │   (T2)       │  GVN, EA, LICM, BCE, GCM, OSR
              └──────┬───────┘
                     │ after K invocations + PGO stability
                     ▼
              ┌──────────────┐
              │   DIAMOND    │  Tier 3 — Peak AOT/JIT Hybrid Optimizer
              │   (T3)       │  PEA, SRA, SLP, vectorization, devirtualization
              └──────────────┘
```

| Tier | Name | C# role | Java role |
| :-- | :-- | :-- | :-- |
| T0 | `granit`  | CIL interpreter. Reads `.dll`/`.exe` PE files, parses metadata, executes CIL on a typed eval stack. | JVM interpreter. Reads `.class` files, parses constant pool, executes JVM bytecodes. |
| T1 | `JADE`   | Baseline SSA JIT. Monomorphic IC stubs for `callvirt`. | Baseline SSA JIT. Monomorphic IC stubs for `invokevirtual`/`invokeinterface`. |
| T2 | `RUBY`   | SoN optimizing JIT. PEA for `box`/`unbox`. LICM for `ldlen`. | SoN optimizing JIT. PEA for `StringBuilder`/`ArrayList`. LICM for `arraylength`. |
| T3 | `DIAMOND`| Peak optimizer. SLP for `Vector<T>`. Speculative devirt via CHA. WPD in AOT. | Peak optimizer. SLP for `jdk.incubator.vector`. Speculative devirt via CHA. WPD (GraalVM Native Image style). |

See [`docs/00-doctrine.md`](docs/00-doctrine.md) for the full doctrine, [`docs/08-csharp-target.md`](docs/08-csharp-target.md) for C#-specific design, and [`docs/09-java-target.md`](docs/09-java-target.md) for Java-specific design.

## Build

Requirements:
- A C++23 compiler (g++ ≥ 14, clang++ ≥ 18, MSVC ≥ 19.40)
- CMake ≥ 3.24

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DJADE_BUILD_TESTS=ON
cmake --build build -j

# Run tests
ctest --test-dir build --output-on-failure

# Run the driver (executes a built-in demo program)
./build/bin/jadec
./build/bin/jadec --dump-ir
./build/bin/jadec -O2 --dump-ir

# Validate doc headers
python3 tools/check_doc_headers.py
```

## Documentation

The full doctrine lives in [`docs/`](docs/), with every file following the mandatory standards (see [`docs/00-doctrine.md`](docs/00-doctrine.md)):

### Doctrine & Philosophy
- [`00-doctrine.md`](docs/00-doctrine.md) — Overview + 4-tier pipeline
- [`01-laws.md`](docs/01-laws.md) — Performance & correctness laws (A, B, C, Rules 36–52)
- [`02-son-ir.md`](docs/02-son-ir.md) — Sea of Nodes IR design

### Architecture & Verification
- [`ARCHITECTURE.md`](docs/ARCHITECTURE.md) — Memory model, threading, IR design, pipeline overview
- [`DEOPT_PROTOCOL.md`](docs/DEOPT_PROTOCOL.md) — FrameState layout, reconstruction, guard types, A.4 proof
- [`BYTECODE_SPEC.md`](docs/BYTECODE_SPEC.md) — **Full** CIL + JVM opcode coverage, unified type lattice, exception tables
- [`04-cpp23.md`](docs/04-cpp23.md) — C++23 usage rules

### Passes & Optimization
- [`PASS_LIST.md`](docs/PASS_LIST.md) — Pass catalogue, dependencies, idempotency/monotonicity proofs
- [`passes/pea_specification.md`](docs/passes/pea_specification.md) — Per-pass spec following the mandatory template
- [`06-optimization-catalog.md`](docs/06-optimization-catalog.md) — Advanced optimizations
- [`07-standard-catalog.md`](docs/07-standard-catalog.md) — Standard optimization catalogue

### Testing & Performance
- [`TESTING_DOCTRINE.md`](docs/TESTING_DOCTRINE.md) — Rule 36/37/38/42 enforcement
- [`03-testing.md`](docs/03-testing.md) — Original CI layout
- [`BENCHMARK_RECORD.md`](docs/BENCHMARK_RECORD.md) — Gold-standard benchmark history

### Target Language Specs
- [`08-csharp-target.md`](docs/08-csharp-target.md) — C# / CIL target specification
- [`09-java-target.md`](docs/09-java-target.md) — Java / JVM target specification

## Project Layout

```
.JADE/
├── CMakeLists.txt
├── cmake/                     # CMake helpers (compiler flags)
├── docs/                      # Doctrine (markdown) — scrapable YAML headers
│   └── passes/                # Per-pass specifications
├── src/jade/
│   ├── core/                  # Arena, Result, NodeId, Flags (Rule 51)
│   ├── ir/                    # Node, Graph, Verifier (Rule 42), Passes
│   │   └── passes/            # ConstantFolding, DCE, GVN, PassPipeline
│   ├── cil/                   # CIL bytecode + CIL→SoN Lowerer (ECMA-335, full)
│   ├── jvm/                   # JVM bytecode + JVM→SoN Lowerer (JVMS §6.5, full)
│   ├── metadata/              # PE/JAR metadata parsers (planned)
│   ├── runtime/               # Safepoint, EpochGC (Rule C.4)
│   ├── tier0_granit/          # Bytecode, Value (CLR/JVM type system), Interpreter
│   ├── tier1_jade/            # Baseline SSA JIT (planned)
│   ├── tier2_ruby/            # Sea of Nodes JIT (planned)
│   ├── tier3_diamond/         # Peak optimizer (planned)
│   └── driver/                # CLI driver (jadec)
├── tests/
│   ├── unit/                  # 207 unit tests (GoogleTest) — all passing
│   ├── golden/                # Golden IR tests (Rule 37) — planned
│   ├── regression/            # Bug-fix regression tests (Rule 36) — planned
│   └── differential/          # Differential tests vs .NET/OpenJDK (Rule 38) — planned
├── tools/                     # CI scripts (benchmark record, doc checker, etc.)
└── benchmarks/                # C# / Java benchmark suite — planned
```

## Implementation Status

### Done
- ✅ CMake build system with `-fno-exceptions -fno-rtti` on the JIT hot path (B.2, B.3)
- ✅ `Arena` / `EdgePool` (Rule B.1)
- ✅ `Result<T>` (= `std::expected<T, Error>`) (Rule B.2)
- ✅ `NodeId`, `FrameStateId`, `ShapeId`, `StringId` — stable IDs (SoN Rule 2)
- ✅ `Flags<E>` type-safe bitmask wrapper (Rule 51) with symbolic printing
- ✅ `Node` value object (~32 bytes target) (SoN Rule 1)
- ✅ `NodeKind` flat enum + metadata table, extended with C#-specific ops (`Box`, `Unbox`, `IsInst`, `CastClass`, `NewObj`, `CallVirt`, `Constrained`, `LdFld`/`StFld`, `LdElem`/`StElem`, `NewArr`, `LdNull`, `LdStr`, `Conv*`, `LdLoc`/`StLoc`/`LdArg`/`StArg`, `Throw`/`Rethrow`/`Leave`/`EndFinally`) and Java-specific ops (`MonitorEnter`, `MonitorExit`, `InvokeDynamic`)
- ✅ `TypeId` lattice (unified for C# and Java)
- ✅ `Graph` with edge pool, side data, debug printer
- ✅ `Verifier` (Rule 42) — 6 invariants checked
- ✅ `EpochGC` — Epoch-Based Reclamation (Rule C.4)
- ✅ `SafepointManager` — safepoint polling (Definition of Done #5)
- ✅ Tier 0 `granit` interpreter with CLR/JVM-flavored `Value` type
- ✅ **Full CIL bytecode module** (ECMA-335) — 256-entry opcode table, all operand formats, two-byte (0xFE) opcodes
- ✅ **Full JVM bytecode module** (JVMS §6.5) — all opcodes including `wide` prefix, `tableswitch`/`lookupswitch`, `invokedynamic`, `multianewarray`
- ✅ CIL → SoN IR lowering (`CilLowerer`)
- ✅ JVM → SoN IR lowering (`JvmLowerer`)
- ✅ Three optimization passes: `ConstantFolding`, `GVN`, `DCE` — all work on both CIL-lowered and JVM-lowered graphs
- ✅ 207 unit tests across core, IR, verifier, passes, CIL opcodes, JVM opcodes, CIL lowering, JVM lowering, granit value type, interpreter, EBR, safepoint — all passing
- ✅ Documentation overhaul: 17 markdown files with mandatory YAML front-matter, including `ARCHITECTURE.md`, `PASS_LIST.md`, `DEOPT_PROTOCOL.md`, `BYTECODE_SPEC.md`, `TESTING_DOCTRINE.md`, `BENCHMARK_RECORD.md`, and `passes/pea_specification.md`
- ✅ CI scripts: `tools/check_doc_headers.py`, `tools/update_benchmark_record.py`

### In Progress
- 🚧 Tier 1 `JADE` — baseline SSA JIT (asmjit integration)
- 🚧 Tier 2 `RUBY` — full SoN lowering + GCM + LICM + BCE for C# and Java patterns
- 🚧 Real CIL interpreter in `granit` — executing decoded CIL bytecode
- 🚧 Real JVM interpreter in `granit` — executing decoded JVM bytecode

### TODO
- ⬜ Tier 3 `DIAMOND` — PEA, SRA, SLP vectorization for `Vector<T>` (C#) and `jdk.incubator.vector` (Java)
- ⬜ `enkiTS` integration for the compiler pool (Definition of Done #6)
- ⬜ `asmjit` integration for code emission
- ⬜ PE file / metadata table parsing (`#~`, `#Strings`, `#US` heaps) for C#
- ⬜ JAR/`.class` file parsing for Java
- ⬜ Golden IR test suite (Rule 37) — ≥10 per pass
- ⬜ Differential testing against .NET CLR (C#) and OpenJDK (Java) (Rule 38)
- ⬜ Macro-benchmark suite (`benchmarks/csharp/`, `benchmarks/java/`)

## License

See [`LICENSE`](LICENSE).


