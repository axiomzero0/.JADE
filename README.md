# .JADE

A 4-tier, profile-driven, speculation-heavy JIT compiler for a dynamic language, written in C++23.

> **Status:** Early development. The initial milestone (see `docs/05-milestone.md`) is the goal of the current sprint.

## The 4-Tier Pipeline

| Tier | Name | Role |
| :-- | :-- | :-- |
| T0 | `granit`  | Register-style interpreter — zero compilation latency, profile collection |
| T1 | `JADE`   | Baseline SSA JIT — eliminate dispatch overhead in milliseconds |
| T2 | `RUBY`   | Sea of Nodes optimizing JIT — GVN, escape analysis, LICM, GCM, OSR |
| T3 | `DIAMOND`| Peak AOT/JIT hybrid — PEA, SRA, SLP, vectorization, devirtualization |

See [`docs/00-doctrine.md`](docs/00-doctrine.md) for the full doctrine.

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
│   ├── runtime/            # Safepoint, Epoch GC (Rule C.4)
│   ├── tier0_granit/       # Bytecode, Interpreter
│   ├── tier1_jade/         # Baseline SSA JIT (stub)
│   ├── tier2_ruby/         # Sea of Nodes JIT (stub)
│   ├── tier3_diamond/      # Peak optimizer (stub)
│   └── driver/             # CLI driver
├── tests/
│   ├── unit/               # Unit tests (GoogleTest)
│   ├── golden/             # Golden IR tests (Rule 37) — TODO
│   └── differential/        # Differential tests (Rule 38) — TODO
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
- ✅ `NodeKind` flat enum + metadata table (SoN Rule 1, B.3)
- ✅ `TypeId` lattice (for SCCP, type narrowing)
- ✅ `Graph` with edge pool, side data, debug printer
- ✅ `Verifier` (Rule 42) — 6 invariants checked
- ✅ `EpochGC` — Epoch-Based Reclamation (Rule C.4)
- ✅ `SafepointManager` — safepoint polling (Definition of Done #5)
- ✅ Tier 0 `granit` bytecode + interpreter (Definition of Done #1)
- ✅ Three optimization passes: `ConstantFolding`, `GVN`, `DCE`
- ✅ Pass pipeline (Rule B.5 — idempotent; Rule B.6 — monotonic)
- ✅ Unit tests: 60+ tests across core, IR, verifier, passes, interpreter, EBR, safepoint

### In Progress
- 🚧 Tier 1 `JADE` — baseline SSA JIT (asmjit integration)
- 🚧 Tier 2 `RUBY` — full Sea of Nodes lowering + GCM + LICM + BCE

### TODO
- ⬜ Tier 3 `DIAMOND` — PEA, SRA, SLP, vectorization
- ⬜ `enkiTS` integration for the compiler pool (Definition of Done #6)
- ⬜ `asmjit` integration for code emission
- ⬜ Golden IR test suite (Rule 37) — ≥10 per pass
- ⬜ Differential testing harness (Rule 38)
- ⬜ `.jade` source parser — currently only runs the built-in demo program

## License

See [`LICENSE`](LICENSE).
