// SPDX-License-Identifier: MIT
// C Macrobenchmark Suite — equivalent to .JADE macro_bench.cpp
// Compile: gcc -O3 -o c_macro_bench c_macro_bench.c
// Run:     ./c_macro_bench 100000000 5

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

static double now_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int64_t json_scan(int64_t n) {
    int64_t count = 0;
    for (int64_t i = 0; i < n; i++) {
        int64_t ch = i & 127;
        if (ch != 32 && ch != 9 && ch != 10) count++;
    }
    return count;
}

static int64_t ecs_update(int64_t n) {
    int64_t sum = 0;
    for (int64_t i = 0; i < n; i++) sum += i * 5 + 3;
    return sum;
}

static int64_t matmul_inner(int64_t n) {
    int64_t sum = 0;
    for (int64_t i = 0; i < n; i++) sum += i * i;
    return sum;
}

static int64_t string_intern(int64_t n) {
    int64_t hash = 0;
    for (int64_t i = 0; i < n; i++) hash = hash * 31 + i;
    return hash;
}

static int64_t expr_eval(int64_t n) {
    int64_t result = 0;
    for (int64_t i = 0; i < n; i++) {
        if (i & 1) result += i * 3;
        else result += i * 5;
    }
    return result;
}

static int64_t arithmetic_loop(int64_t n) {
    int64_t sum = 0;
    for (int64_t i = 0; i < n; i++) sum += i * 3 + 1;
    return sum;
}

static int64_t constant_folding(int64_t n) {
    int64_t sum = 0;
    for (int64_t i = 0; i < n; i++) sum += (3 + 4) * 5;
    return sum;
}

static int64_t dead_code_elim(int64_t n) {
    int64_t sum = 0;
    for (int64_t i = 0; i < n; i++) {
        sum += i;
        volatile int64_t dead = i * 17 + 42;
        dead = dead - dead;
        (void)dead;
    }
    return sum;
}

int main(int argc, char** argv) {
    int64_t N = argc > 1 ? atoll(argv[1]) : 100000000LL;
    int trials = argc > 2 ? atoi(argv[2]) : 5;

    struct { const char* name; int64_t (*fn)(int64_t); } benches[] = {
        {"arithmetic_loop",    arithmetic_loop},
        {"constant_folding",   constant_folding},
        {"dead_code_elim",     dead_code_elim},
        {"json_scan",          json_scan},
        {"ecs_update",         ecs_update},
        {"matmul_inner",       matmul_inner},
        {"string_intern",      string_intern},
        {"expr_eval",          expr_eval},
    };

    printf("=== C Benchmark Suite (gcc -O3) ===\n");
    printf("Iterations: %lld, Trials: %d\n\n", (long long)N, trials);

    for (int b = 0; b < 8; b++) {
        benches[b].fn(N);
        double start = now_sec();
        for (int t = 0; t < trials; t++) benches[b].fn(N);
        double elapsed = now_sec() - start;
        double ns_per = elapsed / trials / N * 1e9;
        printf("%-22s %10.3f ns/iter\n", benches[b].name, ns_per);
    }
    return 0;
}
