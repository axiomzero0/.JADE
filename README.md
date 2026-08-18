# .JADE

A 4-tier, profile-driven, speculation-heavy JIT compiler for **C#**, written in C++23.

> **Status:** Early development. The initial milestone (see `docs/05-milestone.md`) is the goal of the current sprint. The compiler consumes CIL (ECMA-335) bytecode — as produced by Roslyn — and lowers it to a Sea of Nodes IR for optimization.

## The 4-Tier Pipeline

| Tier | Name | Role in a C# context |
| :-- | :-- | :-- |
| T0 | `granit`  | CIL interpreter. Reads `.dll`/`.exe` PE files, parses metadata, executes CIL opcodes on a typed evaluation stack. Collects type feedback. Polls safepoints at back-edges. |
| T1 | `JADE`   | Baseline SSA JIT. Lowers CIL to flat SSA. Fast Linear-Scan register allocation. Monomorphic IC stubs for `callvirt`. |
| T2 | `RUBY`   | Sea of Nodes optimizing JIT. Full SoN IR with explicit effect chains. GVN, escape analysis, LICM, BCE, GCM, OSR. Scalar-replaces value types. |
| T3 | `DIAMOND`| Peak AOT/JIT hybrid. Partial Escape Analysis for boxed value types. SLP auto-vectorization for `Vector<T>`. Speculative devirtualization with CHA. WPD in AOT mode. |

See [`docs/00-doctrine.md`](docs/00-doctrine.md) for the full doctrine and [`docs/08-csharp-target.md`](docs/08-csharp-target.md) for the C#-specific design.

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
```

## Documentation

The full doctrine lives in [`docs/`](docs/):

- [`00-doctrine.md`](docs/00-doctrine.md) — Overview + 4-tier pipeline
- [`01-laws.md`](docs/01-laws.md) — Performance & correctness laws (A, B, C, Rules 36–52)
- [`02-son-ir.md`](docs/02-son-ir.md) — Sea of Nodes IR design
- [`03-testing.md`](docs/03-testing.md) — Testing, debugging, regression
- [`04-cpp23.md`](docs/04-cpp23.md) — C++23 usage rules
- [`05-milestone.md`](docs/05-milestone.md) — Definition of Done (initial)
- [`06-optimization-catalog.md`](docs/06-optimization-catalog.md) — Advanced optimizations
- [`07-standard-catalog.md`](docs/07-standard-catalog.md) — Standard optimization catalogue
- [`08-csharp-target.md`](docs/08-csharp-target.md) — **C# / CIL target specification**

## Project Layout

```
.JADE/
├── CMakeLists.txt
├── cmake/                  # CMake helpers (compiler flags)
├── docs/                   # Doctrine (markdown)
├── src/jade/
│   ├── core/               # Arena, Result, NodeId, Flags (Rule 51)
│   ├── ir/                 # Node, Graph, Verifier (Rule 42), Passes
│   │   └── passes/         # ConstantFolding, DCE, GVN, Pipeline
│   ├── cil/                # CIL bytecode (Opcode.hpp), CIL→SoN Lowerer
│   ├── runtime/            # Safepoint, Epoch GC (Rule C.4)
│   ├── tier0_granit/       # Bytecode, Value (CLR type system), Interpreter
│   ├── tier1_jade/         # Baseline SSA JIT (stub)
│   ├── tier2_ruby/         # Sea of Nodes JIT (stub)
│   ├── tier3_diamond/     # Peak optimizer (stub)
│   └── driver/             # CLI driver
├── tests/
│   └── unit/               # 162 unit tests (GoogleTest)
└── third_party/            # asmjit, enkiTS — TODO
```

## Implementation Status

### Done
- ✅ CMake build system with `-fno-exceptions -fno-rtti` on the JIT hot path (B.2, B.3)
- ✅ `Arena` / `EdgePool` (Rule B.1)
- ✅ `Result<T>` (= `std::expected<T, Error>`) (Rule B.2)
- ✅ `NodeId`, `FrameStateId`, `ShapeId`, `StringId` — stable IDs (SoN Rule 2)
- ✅ `Flags<E>` type-safe bitmask wrapper (Rule 51) with symbolic printing
- ✅ `Node` value object (~32 bytes target) (SoN Rule 1)
- ✅ `NodeKind` flat enum + metadata table, **extended with C#-specific ops**: `Box`, `Unbox`, `IsInst`, `CastClass`, `NewObj`, `CallVirt`, `Constrained`, `LdFld`/`StFld`, `LdElem`/`StElem`, `NewArr`, `LdNull`, `LdStr`, `Conv*`, `LdLoc`/`StLoc`/`LdArg`/`StArg`, `Throw`/`Rethrow`/`Leave`/`EndFinally`
- ✅ `TypeId` lattice (for SCCP, type narrowing)
- ✅ `Graph` with edge pool, side data, debug printer
- ✅ `Verifier` (Rule 42) — 6 invariants checked
- ✅ `EpochGC` — Epoch-Based Reclamation (Rule C.4)
- ✅ `SafepointManager` — safepoint polling (Definition of Done #5)
- ✅ Tier 0 `granit` interpreter with CLR-flavored `Value` type (int32/int64/float/object-ref/managed-ptr/native-ptr)
- ✅ **CIL bytecode module** (ECMA-335 subset) — full opcode decoder with operand format table
- ✅ **CIL → SoN IR lowering** (`CilLowerer`) — handles ldc.i4/ldloc/stloc/ldarg/add/sub/mul/div/ceq/box/unbox/castclass/isinst/conv/ret
- ✅ Three optimization passes: `ConstantFolding`, `GVN`, `DCE` — all work on CIL-lowered graphs
- ✅ 162 unit tests across core, IR, verifier, passes, CIL opcodes, CIL lowering, C# IR nodes, granit value type, interpreter, EBR, safepoint — all passing

### In Progress
- 🚧 Tier 1 `JADE` — baseline SSA JIT (asmjit integration)
- 🚧 Tier 2 `RUBY` — full SoN lowering + GCM + LICM + BCE for C# patterns
- 🚧 CIL interpreter (granit executing real CIL bytecode, not just the legacy Op enum)

### TODO
- ⬜ Tier 3 `DIAMOND` — PEA, SRA, SLP vectorization for C# (`Vector<T>`, `Span<T>`)
- ⬜ `enkiTS` integration for the compiler pool (Definition of Done #6)
- ⬜ `asmjit` integration for code emission
- ⬜ PE file / metadata table parsing (`#~`, `#Strings`, `#US` heaps)
- ⬜ Golden IR test suite (Rule 37) — ≥10 per pass
- ⬜ Differential testing against CoreCLR (Rule 38)

## License

See [`LICENSE`](LICENSE).

