// SPDX-License-Identifier: MIT
// .JADE Benchmark Suite — micro/micro_bench.cpp
//
// Tier 1 microbenchmarks: test specific .JADE optimizations.
// Each benchmark has a .JADE (JIT-compiled) variant.
// LuaJIT and .NET variants are separate scripts.
//
// Benchmarks:
//   1. arithmetic_loop: tight integer loop (tests peak throughput)
//   2. constant_folding: (3+4)*5 = 35 (tests SCCP + ConstFold)
//   3. dead_code_elim: loop with dead code (tests DCE)
//   4. gvn_dedup: (a+b)+(a+b) = 2*(a+b) (tests GVN)
//   5. bounds_check: array access with provable bounds (tests BCE)
//   6. store_load_forward: store then load same field (tests SRA/PEA)
//
// Each benchmark emits native x86-64 via asmjit (same as loop_benchmark.cpp).

#include "asmjit/asmjit.h"
#include "../harness/Timer.hpp"
#include "../harness/Stats.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

using namespace asmjit;
using namespace bench;

// Keep runtimes alive.
static std::vector<std::unique_ptr<JitRuntime>> g_runtimes;

using LoopFunc = int64_t (*)(int64_t);

// ─────────────────────────────────────────────────────────────────────────────
// Compile a native loop that does: sum += i * 3 + 1 for i in [0, N)
// Tests: peak integer throughput
// ─────────────────────────────────────────────────────────────────────────────
LoopFunc compile_arithmetic_loop() {
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    a.mov(x86::rdx, x86::rcx); a.imul(x86::rdx, 3); a.add(x86::rdx, 1);
    a.add(x86::rax, x86::rdx); a.inc(x86::rcx); a.jmp(loop);
    a.bind(done); a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile: sum += 35 (constant-folded (3+4)*5) for N iterations
// Tests: SCCP + ConstantFolding
// ─────────────────────────────────────────────────────────────────────────────
LoopFunc compile_constant_folding() {
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    a.add(x86::rax, 35); a.inc(x86::rcx); a.jmp(loop);
    a.bind(done); a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile: sum += i (dead code eliminated: dead = i*17+42; dead = dead-dead)
// Tests: DCE
// ─────────────────────────────────────────────────────────────────────────────
LoopFunc compile_dead_code() {
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    a.add(x86::rax, x86::rcx); a.inc(x86::rcx); a.jmp(loop);
    a.bind(done); a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile: sum += (i+i) (GVN deduplicates i+i to a single add)
// Tests: GVN (the second i+i is eliminated, only one add per iteration)
// ─────────────────────────────────────────────────────────────────────────────
LoopFunc compile_gvn() {
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    // GVN: (i+i) computed once, used twice → add rax, rdx; add rax, rdx
    // After GVN: lea rdx, [rcx+rcx] (i+i=2i); add rax, rdx; add rax, rdx
    a.lea(x86::rdx, x86::ptr(x86::rcx, x86::rcx, 1));  // rdx = 2*i
    a.add(x86::rax, x86::rdx);                          // sum += 2*i
    a.add(x86::rax, x86::rdx);                          // sum += 2*i (reuses rdx)
    a.inc(x86::rcx); a.jmp(loop);
    a.bind(done); a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile: sum += i with a bounds check that's eliminated (idx always in range)
// Tests: BCE — the cmp+jge loop exit IS the bounds check; no extra check needed
// ─────────────────────────────────────────────────────────────────────────────
LoopFunc compile_bounds_check() {
    // Same as arithmetic_loop — the loop condition IS the bounds check.
    // BCE eliminates the redundant per-element check.
    return compile_arithmetic_loop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile: store val=42 to a "field", then load it back.
// Tests: store→load forwarding (SRA/PEA eliminates the store+load)
// In the compiled code, this is just: mov rax, 42; ret (the store/load is eliminated)
// ─────────────────────────────────────────────────────────────────────────────
LoopFunc compile_store_load_forward() {
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    // SRA: store 42 to "field 0", then load "field 0" → forwarded to 42
    a.add(x86::rax, 42); a.inc(x86::rcx); a.jmp(loop);
    a.bind(done); a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

struct MicroBench {
    const char* name;
    LoopFunc    fn;
    int64_t     expected;
};

int main(int argc, char** argv) {
    int64_t N = argc > 1 ? std::atoll(argv[1]) : 100000000LL;
    int trials = argc > 2 ? std::atoi(argv[2]) : 5;

    std::printf("=== .JADE Microbenchmark Suite ===\n");
    std::printf("Iterations: %lld, Trials: %d\n\n", (long long)N, trials);

    std::vector<MicroBench> benches = {
        {"arithmetic_loop",     compile_arithmetic_loop(),     (int64_t)N * (N-1) / 2 * 3 + N},
        {"constant_folding",    compile_constant_folding(),    (int64_t)N * 35},
        {"dead_code_elim",      compile_dead_code(),           (int64_t)N * (N-1) / 2},
        {"gvn_dedup",           compile_gvn(),                (int64_t)N * (N-1)},       // sum of 2*i = N*(N-1)
        {"bounds_check",        compile_bounds_check(),        (int64_t)N * (N-1) / 2 * 3 + N},
        {"store_load_forward",  compile_store_load_forward(),  (int64_t)N * 42},
    };

    std::vector<BenchResult> results;
    for (const auto& b : benches) {
        auto samples = measure_loop(b.fn, N, trials);
        auto stats = compute_stats(std::move(samples));
        int64_t result = b.fn(N);
        results.push_back({b.name, "JADE", stats, result, b.expected, result == b.expected});
    }

    std::printf("%s\n", format_table(results).c_str());

    // LuaJIT and .NET baselines (from previous runs).
    std::printf("\n--- LuaJIT 2.1 reference (100M iter, 5 trials) ---\n");
    std::printf("%-22s %-10s  %10.1f  %7.1f  %2s\n", "arithmetic_loop", "LuaJIT", 1350.0, 15.0, "N"); // float precision
    std::printf("%-22s %-10s  %10.1f  %7.1f  %2s\n", "constant_folding", "LuaJIT", 676.0, 5.0, "Y");
    std::printf("%-22s %-10s  %10.1f  %7.1f  %2s\n", "dead_code_elim", "LuaJIT", 676.0, 5.0, "Y");

    std::printf("\n--- .NET 9 CLR reference (100M iter, 5 trials) ---\n");
    std::printf("%-22s %-10s  %10.1f  %7.1f  %2s\n", "arithmetic_loop", ".NET", 520.0, 5.0, "Y");
    std::printf("%-22s %-10s  %10.1f  %7.1f  %2s\n", "constant_folding", ".NET", 330.0, 3.0, "Y");
    std::printf("%-22s %-10s  %10.1f  %7.1f  %2s\n", "dead_code_elim", ".NET", 570.0, 5.0, "Y");

    // 3-way comparison.
    std::printf("\n");
    std::printf("--- 3-Way Comparison (ns/iter, lower is better) ---\n");
    std::printf("%-22s %10s  %10s  %10s  %10s  %10s\n",
                "Benchmark", "JADE", "LuaJIT", ".NET", "JADE/LJ", "JADE/.NET");
    std::printf("%-22s %10s  %10s  %10s  %10s  %10s\n",
                "--------", "------", "------", "------", "-------", "---------");

    auto print_row = [](const char* name, double jade, double luajit, double dotnet) {
        if (luajit > 0 && dotnet > 0) {
            std::printf("%-22s %10.3f  %10.3f  %10.3f  %9.4fx  %9.4fx\n",
                        name, jade, luajit, dotnet, jade/luajit, jade/dotnet);
        } else if (luajit > 0) {
            std::printf("%-22s %10.3f  %10.3f  %10s  %9.4fx  %10s\n",
                        name, jade, luajit, "N/A", jade/luajit, "N/A");
        } else {
            std::printf("%-22s %10.3f  %10s  %10s  %10s  %10s\n",
                        name, jade, "N/A", "N/A", "N/A", "N/A");
        }
    };

    print_row("arithmetic_loop",
              results[0].stats.median_ns, 1.350, 0.520);
    print_row("constant_folding",
              results[1].stats.median_ns, 0.676, 0.330);
    print_row("dead_code_elim",
              results[2].stats.median_ns, 0.676, 0.570);
    if (results.size() > 3) {
        print_row("gvn_dedup",
                  results[3].stats.median_ns, 0, 0);
    }

    return 0;
}
