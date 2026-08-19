// SPDX-License-Identifier: MIT
// .JADE Benchmark Suite — macro/macro_bench.cpp
//
// Tier 2: Macrobenchmarks — realistic workload simulations.

#include "asmjit/asmjit.h"
#include "../harness/Timer.hpp"
#include "../harness/Stats.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <vector>

using namespace asmjit;
using namespace bench;

static std::vector<std::unique_ptr<JitRuntime>> g_runtimes;
using LoopFunc = int64_t (*)(int64_t);

// 1. JSON scan: char classification loop
LoopFunc compile_json_scan() {
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label(), not_space = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    a.mov(x86::rdx, x86::rcx); a.and_(x86::rdx, 127);
    a.cmp(x86::rdx, 32); a.jne(not_space);
    a.cmp(x86::rdx, 9); a.jne(not_space);
    a.cmp(x86::rdx, 10); a.jne(not_space);
    a.inc(x86::rax); a.bind(not_space); a.inc(x86::rcx); a.jmp(loop);
    a.bind(done); a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

// 2. ECS update: position += velocity * dt
LoopFunc compile_ecs_update() {
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    a.mov(x86::rdx, x86::rcx); a.imul(x86::rdx, 5); a.add(x86::rdx, 3);
    a.add(x86::rax, x86::rdx); a.inc(x86::rcx); a.jmp(loop);
    a.bind(done); a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

// 3. Matmul inner: sum += i*i (dot product)
LoopFunc compile_matmul_inner() {
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    a.mov(x86::rdx, x86::rcx); a.imul(x86::rdx, x86::rcx);
    a.add(x86::rax, x86::rdx); a.inc(x86::rcx); a.jmp(loop);
    a.bind(done); a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

// 4. String interner: hash = hash * 31 + i
LoopFunc compile_string_intern() {
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    a.imul(x86::rax, 31); a.add(x86::rax, x86::rcx);
    a.inc(x86::rcx); a.jmp(loop);
    a.bind(done); a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

// 5. Expression evaluator: alternating mul by 3 or 5
LoopFunc compile_expr_eval() {
    auto rt = std::make_unique<JitRuntime>();
    CodeHolder code; code.init(rt->environment());
    x86::Assembler a(&code);
    a.push(x86::rbp); a.mov(x86::rbp, x86::rsp);
    a.xor_(x86::rax, x86::rax); a.xor_(x86::rcx, x86::rcx);
    Label loop = a.new_label(), done = a.new_label();
    Label odd = a.new_label(), merge = a.new_label();
    a.bind(loop); a.cmp(x86::rcx, x86::rdi); a.jge(done);
    a.test(x86::rcx, 1); a.jne(odd);
    a.imul(x86::rdx, x86::rcx, 5); a.jmp(merge);
    a.bind(odd); a.imul(x86::rdx, x86::rcx, 3);
    a.bind(merge); a.add(x86::rax, x86::rdx);
    a.inc(x86::rcx); a.jmp(loop);
    a.bind(done); a.mov(x86::rsp, x86::rbp); a.pop(x86::rbp); a.ret();
    void* e = nullptr; rt->add(&e, &code); g_runtimes.push_back(std::move(rt));
    return reinterpret_cast<LoopFunc>(e);
}

int main(int argc, char** argv) {
    int64_t N = argc > 1 ? std::atoll(argv[1]) : 100000000LL;
    int trials = argc > 2 ? std::atoi(argv[2]) : 5;

    std::printf("=== .JADE Macrobenchmark Suite ===\n");
    std::printf("Iterations: %lld, Trials: %d\n\n", (long long)N, trials);

    struct MB { const char* name; LoopFunc fn; };
    std::vector<MB> benches = {
        {"json_scan",      compile_json_scan()},
        {"ecs_update",     compile_ecs_update()},
        {"matmul_inner",   compile_matmul_inner()},
        {"string_intern",  compile_string_intern()},
        {"expr_eval",      compile_expr_eval()},
    };

    std::printf("%-22s %10s  %10s  %7s  %12s\n",
                "Benchmark", "Median ns", "MAD ns", "Stable", "Result");
    std::printf("%-22s %10s  %10s  %7s  %12s\n",
                "--------", "---------", "------", "-------", "------");
    for (const auto& b : benches) {
        auto samples = measure_loop(b.fn, N, trials);
        auto stats = compute_stats(std::move(samples));
        int64_t result = b.fn(N);
        std::printf("%-22s %10.3f  %10.3f  %7s  %12lld\n",
                    b.name, stats.median_ns, stats.mad_ns,
                    stats.significant ? "Y" : "N", (long long)result);
    }

    std::printf("\n--- Run LuaJIT and .NET equivalents separately ---\n");
    std::printf("LuaJIT: /tmp/luajit-src/src/luajit benchmarks/luajit/<bench>.lua %lld %d\n",
                (long long)N, trials);
    std::printf(".NET:   dotnet run -c Release -- benchmarks/csharp/<bench> %lld %d\n",
                (long long)N, trials);
    return 0;
}
