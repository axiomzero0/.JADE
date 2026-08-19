// SPDX-License-Identifier: MIT
// .JADE Benchmark Suite — startup/startup_bench.cpp
//
// Tier 4: Startup & warmup benchmarks.
// Measures: cold start to first result, warm-to-peak latency, compile time.
//
// Since .JADE doesn't have a runtime with dynamic tiering yet, this
// measures the static compile time of the JADE JIT pipeline:
//   1. Build IR graph
//   2. Run RUBY pipeline
//   3. Run DIAMOND pipeline
//   4. Compile with JADE JIT (LSRA + asmjit)
//   5. Execute

#include "jade/ir/Graph.hpp"
#include "jade/ir/passes/PassPipeline.hpp"
#include "jade/tier1_jade/JadeJit.hpp"
#include "../harness/Timer.hpp"

#include <cstdio>
#include <cstdint>
#include <chrono>

using namespace jade;
using namespace jade::tier1;
using namespace bench;

int main() {
    std::printf("=== .JADE Startup Benchmark ===\n\n");

    // ── Cold compile: build IR → optimize → JIT → execute ──
    Timer t;

    // 1. Build IR
    t.start();
    Graph g;
    GraphBuilder b(g);
    auto start = b.start();
    auto three = b.const_int(3);
    auto four = b.const_int(4);
    auto seven = b.add(three, four);
    auto five = b.const_int(5);
    auto result = b.mul(seven, five);
    auto ret = b.return_node(result);
    g.set_ctrl_input(ret, start);
    g.set_effect_input(ret, start);
    t.stop();
    double ir_ms = t.milliseconds();
    std::printf("1. Build IR:          %8.3f ms\n", ir_ms);

    // 2. RUBY pipeline
    t.start();
    {
        PassContext ctx;
        auto pipe = build_ruby_pipeline();
        auto pr = pipe->run(g, ctx);
    }
    t.stop();
    double ruby_ms = t.milliseconds();
    std::printf("2. RUBY pipeline:     %8.3f ms  (21 passes)\n", ruby_ms);

    // 3. DIAMOND pipeline
    t.start();
    {
        PassContext ctx;
        auto pipe = build_diamond_pipeline();
        auto pr = pipe->run(g, ctx);
    }
    t.stop();
    double diamond_ms = t.milliseconds();
    std::printf("3. DIAMOND pipeline:  %8.3f ms  (28 passes)\n", diamond_ms);

    // 4. JADE JIT compile
    t.start();
    JadeJit jit;
    auto cr = jit.compile(g);
    t.stop();
    double jit_ms = t.milliseconds();
    std::printf("4. JADE JIT compile:  %8.3f ms  (LSRA + asmjit)\n", jit_ms);

    // 5. Execute
    t.start();
    if (cr) {
        auto fn = reinterpret_cast<int64_t(*)()>(cr->entry_point);
        volatile int64_t r = fn();
        (void)r;
    }
    t.stop();
    double exec_ns = t.nanoseconds();
    std::printf("5. Execute:           %8.3f ns\n", exec_ns);

    // Totals
    double total = ir_ms + ruby_ms + diamond_ms + jit_ms;
    std::printf("\nTotal compile time:   %8.3f ms\n", total);
    std::printf("  IR build:            %8.1f%%\n", ir_ms / total * 100);
    std::printf("  RUBY:                %8.1f%%\n", ruby_ms / total * 100);
    std::printf("  DIAMOND:             %8.1f%%\n", diamond_ms / total * 100);
    std::printf("  JADE JIT:            %8.1f%%\n", jit_ms / total * 100);

    std::printf("\n--- Reference (LuaJIT startup) ---\n");
    std::printf("  LuaJIT cold start:  ~1-2 ms (interpreter only)\n");
    std::printf("  .NET cold start:    ~50-100 ms (CLR init + JIT)\n");

    return 0;
}
