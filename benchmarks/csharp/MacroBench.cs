// SPDX-License-Identifier: MIT
// .NET Macrobenchmark Suite — equivalent to .JADE macro_bench.cpp

using System;
using System.Diagnostics;

class MacroBench
{
    static long N = 100_000_000;
    static int trials = 5;

    // 1. JSON scan
    static long JsonScan(long n)
    {
        long count = 0;
        for (long i = 0; i < n; i++)
        {
            long ch = i & 127;
            if (ch != 32 && ch != 9 && ch != 10)
                count++;
        }
        return count;
    }

    // 2. ECS update
    static long EcsUpdate(long n)
    {
        long sum = 0;
        for (long i = 0; i < n; i++)
            sum += i * 5 + 3;
        return sum;
    }

    // 3. Matmul inner
    static long MatmulInner(long n)
    {
        long sum = 0;
        for (long i = 0; i < n; i++)
            sum += i * i;
        return sum;
    }

    // 4. String interner
    static long StringIntern(long n)
    {
        long hash = 0;
        for (long i = 0; i < n; i++)
            hash = hash * 31 + i;
        return hash;
    }

    // 5. Expression evaluator
    static long ExprEval(long n)
    {
        long result = 0;
        for (long i = 0; i < n; i++)
        {
            if ((i & 1) == 1)
                result += i * 3;
            else
                result += i * 5;
        }
        return result;
    }

    static void Main(string[] args)
    {
        if (args.Length > 0) N = long.Parse(args[0]);
        if (args.Length > 1) trials = int.Parse(args[1]);

        Console.WriteLine("=== .NET Macrobenchmark Suite ===");
        Console.WriteLine($"Iterations: {N}, Trials: {trials}\n");

        var benches = new (string name, Func<long, long> fn)[]
        {
            ("json_scan", JsonScan),
            ("ecs_update", EcsUpdate),
            ("matmul_inner", MatmulInner),
            ("string_intern", StringIntern),
            ("expr_eval", ExprEval),
        };

        foreach (var (name, fn) in benches)
        {
            fn(N); // warmup
            var sw = Stopwatch.StartNew();
            for (int t = 0; t < trials; t++)
                fn(N);
            sw.Stop();
            double nsPer = sw.Elapsed.TotalMilliseconds / trials / N * 1e6;
            Console.WriteLine($"{name,-22} {nsPer,10:F3} ns/iter");
        }
    }
}
