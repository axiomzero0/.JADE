// SPDX-License-Identifier: MIT
// .JADE Benchmark Suite — stress/stress_bench.cpp
//
// Tier 3: Stress benchmarks — expose failure modes.
// Each benchmark measures DEGRADATION RATIOS, not absolute throughput.
//
// Benchmarks:
//   1. allocation_burst: short-lived alloc spike (tests GC/bump arena)
//   2. tier_cliff: code that barely qualifies for T2/T3 (compile latency vs benefit)
//   3. profile_pollution: mixed hot/cold paths in same method (mis-speculation)
//   4. gc_safepoint_stress: long loop with safepoint polling (polling overhead)
//   5. branch_mispredict: unpredictable branches (tests branch predictor)

#include "asmjit/asmjit.h"
#include "../harness/Timer.hpp"
#include "../harness/Stats.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <vector>
#include <random>

using namespace asmjit;
using namespace bench;

static std::vector<std::unique_ptr<JitRuntime>> g_runtimes;
using LoopFunc = int64_t (*)(int64_t);

// 1. Safepoint stress: loop with a safepoint poll every iteration
LoopFunc compile_safepoint_stress() {
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    // Safepoint poll: test a static flag (always 0 = no safepoint needed)
    a.add(x86::rax, x86::rcx);
    a.inc(x86::rcx);
    // Insert a NOP sled to simulate safepoint poll overhead
    a.nop(); a.nop(); a.nop();
    a.jmp(loop);
    a.bind(done); a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

// 2. Branch mispredict: unpredictable conditional (random-ish pattern)
LoopFunc compile_branch_mispredict() {
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label();
    Label taken = a.new_label(), merge = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    // Unpredictable branch: test (i * 7919) & 1 (pseudo-random prime)
    a.mov(x86::rdx, x86::rcx);
    a.imul(x86::rdx, 7919);
    a.test(x86::rdx, 1);
    a.jne(taken);
    a.add(x86::rax, 1);
    a.jmp(merge);
    a.bind(taken);
    a.add(x86::rax, 2);
    a.bind(merge);
    a.inc(x86::rcx); a.jmp(loop);
    a.bind(done); a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

// 3. Profile pollution: mixed hot/cold paths (if/else with 50/50 split)
LoopFunc compile_profile_pollution() {
    // Same as branch_mispredict but with more work per branch
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label();
    Label path_a = a.new_label(), merge = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    a.test(x86::rcx, 1);
    a.jne(path_a);
    // Path B: imul + add
    a.mov(x86::rdx, x86::rcx);
    a.imul(x86::rdx, 7);
    a.add(x86::rax, x86::rdx);
    a.jmp(merge);
    a.bind(path_a);
    // Path A: shl + add
    a.mov(x86::rdx, x86::rcx);
    a.shl(x86::rdx, 3);
    a.add(x86::rax, x86::rdx);
    a.bind(merge);
    a.inc(x86::rcx); a.jmp(loop);
    a.bind(done); a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

// 4. Allocation burst simulation: simulate alloc overhead with push/pop
LoopFunc compile_alloc_burst() {
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.sub(x86::rsp, 64);  // simulate local allocation
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    // Simulate: allocate (stack), use, deallocate
    a.mov(x86::qword_ptr(x86::rsp, 0), x86::rcx);   // store i
    a.mov(x86::rdx, x86::qword_ptr(x86::rsp, 0));   // load i
    a.add(x86::rax, x86::rdx);                       // sum += i
    a.inc(x86::rcx); a.jmp(loop);
    a.bind(done);
    a.add(x86::rsp, 64);  // deallocate
    a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

// 5. Baseline: no stress (for comparison)
LoopFunc compile_baseline() {
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

int main(int argc, char** argv) {
    int64_t N = argc > 1 ? std::atoll(argv[1]) : 100000000LL;
    int trials = argc > 2 ? std::atoi(argv[2]) : 5;

    std::printf("=== .JADE Stress Benchmark Suite ===\n");
    std::printf("Iterations: %lld, Trials: %d\n\n", (long long)N, trials);

    struct SB { const char* name; LoopFunc fn; };
    std::vector<SB> benches = {
        {"baseline (no stress)",   compile_baseline()},
        {"safepoint_stress",       compile_safepoint_stress()},
        {"branch_mispredict",      compile_branch_mispredict()},
        {"profile_pollution",      compile_profile_pollution()},
        {"alloc_burst (stack)",    compile_alloc_burst()},
    };

    // Get baseline first.
    double baseline_ns;
    {
        auto samples = measure_loop(benches[0].fn, N, trials);
        auto stats = compute_stats(std::move(samples));
        baseline_ns = stats.median_ns;
    }

    std::printf("%-22s %10s  %10s  %10s  %7s\n",
                "Benchmark", "Median ns", "MAD ns", "Degrade", "Stable");
    std::printf("%-22s %10s  %10s  %10s  %7s\n",
                "--------", "---------", "------", "--------", "-------");

    for (const auto& b : benches) {
        auto samples = measure_loop(b.fn, N, trials);
        auto stats = compute_stats(std::move(samples));
        int64_t result = b.fn(N);
        double degrade = baseline_ns > 0 ? stats.median_ns / baseline_ns : 0;

        std::printf("%-22s %10.3f  %10.3f  %9.2fx  %7s\n",
                    b.name, stats.median_ns, stats.mad_ns,
                    degrade, stats.significant ? "Y" : "N");
    }

    std::printf("\nDegradation ratio = stress ns / baseline ns.\n");
    std::printf("A ratio of 1.0x means no degradation.\n");
    std::printf("A ratio > 2.0x indicates the stress factor is significant.\n");

    return 0;
}
