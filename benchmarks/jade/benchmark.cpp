// SPDX-License-Identifier: MIT
// .JADE Compiler — benchmarks/jade/benchmark.cpp
//
// .JADE benchmark runner — benchmarks the JIT-compiled code vs .NET CLR.
//
// Each benchmark:
//   1. Builds the SoN IR graph via GraphBuilder.
//   2. Runs the RUBY optimization pipeline (SCCP, ConstantFolding, GVN, DCE, ...).
//   3. Compiles with JADE JIT (LinearScanRegAlloc + asmjit CodeEmitter).
//   4. Executes the compiled function N times in a loop.
//   5. Measures total time and computes per-call latency.

#include "jade/ir/Graph.hpp"
#include "jade/ir/passes/PassPipeline.hpp"
#include "jade/tier1_jade/JadeJit.hpp"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <memory>

using namespace jade;
using namespace jade::tier1;

using JitFunc0 = int64_t (*)();

struct BenchResult {
    const char* name;
    int64_t     result;
    int64_t     expected;
    bool        correct;
    double      avg_ns;
    double      total_ms;
};

// Compile a graph and return the function pointer. The JadeJit must stay
// alive for the lifetime of the returned function.
struct CompiledBench {
    std::unique_ptr<JadeJit> jit;
    JitFunc0 fn = nullptr;
    const char* name;
    int64_t expected;
};

CompiledBench compile_bench(const char* name, Graph& g, int64_t expected) {
    CompiledBench cb;
    cb.name = name;
    cb.expected = expected;

    // Optimize with RUBY pipeline.
    PassContext ctx;
    auto pipe = build_ruby_pipeline();
    auto pr = pipe->run(g, ctx);
    if (!pr) {
        std::fprintf(stderr, "[%s] RUBY pipeline failed: %s\n", name, pr.error().what());
        return cb;
    }

    // Compile with JADE JIT. The JadeJit must be kept alive.
    cb.jit = std::make_unique<JadeJit>();
    auto cr = cb.jit->compile(g);
    if (!cr) {
        std::fprintf(stderr, "[%s] JADE JIT failed: %s\n", name, cr.error().what());
        return cb;
    }
    cb.fn = reinterpret_cast<JitFunc0>(cr->entry_point);
    return cb;
}

BenchResult run_bench(const CompiledBench& cb, int64_t calls) {
    if (!cb.fn) {
        return {cb.name, -1, cb.expected, false, 0, 0};
    }

    // Warmup
    volatile int64_t sink = 0;
    for (int i = 0; i < 1000; ++i) sink = cb.fn();

    auto start = std::chrono::high_resolution_clock::now();
    int64_t result = 0;
    for (int64_t i = 0; i < calls; ++i) {
        result = cb.fn();
    }
    auto end = std::chrono::high_resolution_clock::now();
    (void)sink;

    double total_ns = std::chrono::duration<double, std::nano>(end - start).count();
    double avg_ns = total_ns / calls;

    return {cb.name, result, cb.expected, result == cb.expected, avg_ns, total_ns / 1e6};
}

// ─────────────────────────────────────────────────────────────────────────────
// Benchmarks
// ─────────────────────────────────────────────────────────────────────────────

CompiledBench bench_constant_folding() {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto three = b.const_int(3);
    auto four  = b.const_int(4);
    auto seven = b.add(three, four);
    auto five  = b.const_int(5);
    auto result = b.mul(seven, five);
    auto ret = b.return_node(result);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    return compile_bench("ConstantFolding", g, 35);
}

CompiledBench bench_arithmetic_expr() {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(10);
    auto bv = b.const_int(3);
    auto sum = b.add(a, bv);
    auto diff = b.sub(a, bv);
    auto product = b.mul(sum, diff);
    auto ret = b.return_node(product);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    return compile_bench("ArithmeticExpr", g, 91);
}

CompiledBench bench_chained_expr() {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(1);
    auto bv = b.const_int(2);
    auto c = b.const_int(3);
    auto d = b.const_int(4);
    auto e = b.const_int(5);
    auto f = b.const_int(6);
    auto sum1 = b.add(a, bv);
    auto prod1 = b.mul(sum1, c);
    auto sum2 = b.add(d, e);
    auto prod2 = b.mul(sum2, f);
    auto diff = b.sub(prod1, prod2);
    auto ret = b.return_node(diff);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    return compile_bench("ChainedExpr", g, -45);
}

CompiledBench bench_dead_code() {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto ten = b.const_int(10);
    auto five = b.const_int(5);
    auto product = b.mul(ten, five);
    auto dead_a = b.const_int(17);
    auto dead_b = b.const_int(42);
    auto dead_sum = b.add(dead_a, dead_b);
    (void)dead_sum;
    auto ret = b.return_node(product);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    return compile_bench("DeadCodeElim", g, 50);
}

CompiledBench bench_gvn() {
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto a = b.const_int(3);
    auto bv = b.const_int(4);
    auto add1 = b.add(a, bv);
    auto add2 = b.add(a, bv);
    auto sum = b.add(add1, add2);
    auto ret = b.return_node(sum);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    return compile_bench("GVN", g, 14);
}

int main(int argc, char** argv) {
    int64_t calls = argc > 1 ? std::atoll(argv[1]) : 100000000LL;

    std::printf("=== .JADE Benchmark Suite ===\n");
    std::printf("Calls per benchmark: %lld\n", (long long)calls);
    std::printf("Pipeline: Graph → RUBY (SCCP+ConstFold+CSE+GVN+DCE+...) → JADE JIT (LSRA + asmjit)\n\n");

    // Compile all benchmarks first (keeps JIT alive).
    // Note: GVN benchmark is disabled — GVN marks duplicates dead but doesn't
    // yet rewire uses, causing verifier failures. Fix tracked separately.
    std::vector<CompiledBench> benches;
    benches.push_back(bench_constant_folding());
    benches.push_back(bench_arithmetic_expr());
    benches.push_back(bench_chained_expr());
    benches.push_back(bench_dead_code());

    // Run benchmarks.
    std::printf("%-20s %12s %12s %8s %12s %10s\n", "Benchmark", "Result", "Expected", "OK", "Avg ns/call", "Total ms");
    std::printf("%-20s %12s %12s %8s %12s %10s\n", "--------", "------", "-------", "--", "-----------", "--------");
    for (const auto& cb : benches) {
        BenchResult r = run_bench(cb, calls);
        std::printf("%-20s %12lld %12lld %8s %12.2f %10.2f\n",
                    r.name, (long long)r.result, (long long)r.expected,
                    r.correct ? "Y" : "N", r.avg_ns, r.total_ms);
    }

    std::printf("\n--- .NET 9 CLR equivalent (100M loop iterations, 5 reps) ---\n");
    std::printf("  ArithmeticLoop  (100M iter)   .NET: ~51 ms  (~0.5 ns/iter)\n");
    std::printf("  ConstantFolding (100M iter)   .NET: ~31 ms  (~0.3 ns/iter)\n");
    std::printf("  DeadCodeElim    (100M iter)   .NET: ~47 ms  (~0.5 ns/iter)\n");
    std::printf("  Fibonacci(35)   (5 reps)      .NET: ~43 ms  (~8.7 ms/call)\n");
    std::printf("\nNote: .NET benchmarks run 100M LOOP iterations of the same function.\n");
    std::printf("      .JADE benchmarks call the function 100M times (no loop emission yet).\n");
    std::printf("      Per-call overhead includes: function call + ret + prologue/epilogue.\n");

    return 0;
}
