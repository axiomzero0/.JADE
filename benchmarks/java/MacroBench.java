// SPDX-License-Identifier: MIT
// Java Macrobenchmark Suite — equivalent to .JADE macro_bench.cpp
// Run: javac MacroBench.java && java -XX:+UseGraalJIT MacroBench 100000000 5
// (OpenJDK 21+ has Graal JIT integrated)

public class MacroBench {
    static long N = 100_000_000;
    static int trials = 5;

    static long jsonScan(long n) {
        long count = 0;
        for (long i = 0; i < n; i++) {
            long ch = i & 127;
            if (ch != 32 && ch != 9 && ch != 10) count++;
        }
        return count;
    }

    static long ecsUpdate(long n) {
        long sum = 0;
        for (long i = 0; i < n; i++) sum += i * 5 + 3;
        return sum;
    }

    static long matmulInner(long n) {
        long sum = 0;
        for (long i = 0; i < n; i++) sum += i * i;
        return sum;
    }

    static long stringIntern(long n) {
        long hash = 0;
        for (long i = 0; i < n; i++) hash = hash * 31 + i;
        return hash;
    }

    static long exprEval(long n) {
        long result = 0;
        for (long i = 0; i < n; i++) {
            if ((i & 1) == 1) result += i * 3;
            else result += i * 5;
        }
        return result;
    }

    static long arithmeticLoop(long n) {
        long sum = 0;
        for (long i = 0; i < n; i++) sum += i * 3 + 1;
        return sum;
    }

    static long constantFolding(long n) {
        long sum = 0;
        for (long i = 0; i < n; i++) sum += (3 + 4) * 5;
        return sum;
    }

    static long deadCodeElim(long n) {
        long sum = 0;
        for (long i = 0; i < n; i++) {
            sum += i;
            long dead = i * 17 + 42;
            dead = dead - dead;
        }
        return sum;
    }

    public static void main(String[] args) {
        if (args.length > 0) N = Long.parseLong(args[0]);
        if (args.length > 1) trials = Integer.parseInt(args[1]);

        System.out.println("=== Java/Graal Macrobenchmark Suite ===");
        System.out.printf("Iterations: %d, Trials: %d%n%n", N, trials);

        String[] names = {"arithmetic_loop", "constant_folding", "dead_code_elim",
                         "json_scan", "ecs_update", "matmul_inner",
                         "string_intern", "expr_eval"};

        for (int b = 0; b < 8; b++) {
            // Warmup
            switch (b) {
                case 0: arithmeticLoop(N); break;
                case 1: constantFolding(N); break;
                case 2: deadCodeElim(N); break;
                case 3: jsonScan(N); break;
                case 4: ecsUpdate(N); break;
                case 5: matmulInner(N); break;
                case 6: stringIntern(N); break;
                case 7: exprEval(N); break;
            }

            long start = System.nanoTime();
            for (int t = 0; t < trials; t++) {
                switch (b) {
                    case 0: arithmeticLoop(N); break;
                    case 1: constantFolding(N); break;
                    case 2: deadCodeElim(N); break;
                    case 3: jsonScan(N); break;
                    case 4: ecsUpdate(N); break;
                    case 5: matmulInner(N); break;
                    case 6: stringIntern(N); break;
                    case 7: exprEval(N); break;
                }
            }
            long elapsed = System.nanoTime() - start;
            double nsPer = (double) elapsed / trials / N;
            System.out.printf("%-22s %10.3f ns/iter%n", names[b], nsPer);
        }
    }
}
