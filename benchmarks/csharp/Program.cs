// SPDX-License-Identifier: MIT
// .NET Benchmark Runner — runs all benchmarks and reports results.
//
// Usage: dotnet run -c Release -- <benchmark_name> [args]
//   benchmark_name: ArithmeticLoop, Fibonacci, ConstantFolding, DeadCodeElim, All

using System;
using System.Diagnostics;

class Program
{
    static void Main(string[] args)
    {
        string bench = args.Length > 0 ? args[0] : "All";
        int n = args.Length > 1 ? int.Parse(args[1]) : 100_000_000;
        int iterations = args.Length > 2 ? int.Parse(args[2]) : 5;

        switch (bench)
        {
            case "ArithmeticLoop":
                RunArithmeticLoop(n, iterations);
                break;
            case "Fibonacci":
                RunFibonacci(35, iterations);   // smaller N for recursive fib
                break;
            case "ConstantFolding":
                RunConstantFolding(n, iterations);
                break;
            case "DeadCodeElim":
                RunDeadCodeElim(n, iterations);
                break;
            case "All":
                RunArithmeticLoop(n, iterations);
                RunFibonacci(35, iterations);
                RunConstantFolding(n, iterations);
                RunDeadCodeElim(n, iterations);
                break;
            default:
                Console.WriteLine($"Unknown benchmark: {bench}");
                Console.WriteLine("Available: ArithmeticLoop, Fibonacci, ConstantFolding, DeadCodeElim, All");
                break;
        }
    }

    // ── ArithmeticLoop ──────────────────────────────────────────────────
    static long ArithmeticLoopCompute(int n)
    {
        long sum = 0;
        for (int i = 0; i < n; i++)
            sum += (long)i * 3 + 1;
        return sum;
    }

    static void RunArithmeticLoop(int n, int iterations)
    {
        ArithmeticLoopCompute(n); // warmup
        long result = 0;
        var sw = Stopwatch.StartNew();
        for (int rep = 0; rep < iterations; rep++)
            result = ArithmeticLoopCompute(n);
        sw.Stop();
        double avgMs = sw.Elapsed.TotalMilliseconds / iterations;
        long expected = (long)n * (n - 1) / 2 * 3 + n;
        Console.WriteLine($"Benchmark: ArithmeticLoop (.NET 9 CLR)");
        Console.WriteLine($"  N = {n}");
        Console.WriteLine($"  Result = {result}");
        Console.WriteLine($"  Expected = {expected}");
        Console.WriteLine($"  Correct = {result == expected}");
        Console.WriteLine($"  Avg time = {avgMs:F3} ms");
    }

    // ── Fibonacci ────────────────────────────────────────────────────────
    static long Fib(int n)
    {
        if (n < 2) return n;
        return Fib(n - 1) + Fib(n - 2);
    }

    static void RunFibonacci(int n, int iterations)
    {
        Fib(10); // warmup
        long result = 0;
        var sw = Stopwatch.StartNew();
        for (int rep = 0; rep < iterations; rep++)
            result = Fib(n);
        sw.Stop();
        double avgMs = sw.Elapsed.TotalMilliseconds / iterations;
        Console.WriteLine($"Benchmark: Fibonacci (.NET 9 CLR)");
        Console.WriteLine($"  N = {n}");
        Console.WriteLine($"  Result = {result}");
        Console.WriteLine($"  Avg time = {avgMs:F3} ms");
    }

    // ── ConstantFolding ──────────────────────────────────────────────────
    static long ConstantFoldingCompute(int n)
    {
        long sum = 0;
        for (int i = 0; i < n; i++)
            sum += (3 + 4) * 5; // = 35, should be folded
        return sum;
    }

    static void RunConstantFolding(int n, int iterations)
    {
        ConstantFoldingCompute(n); // warmup
        long result = 0;
        var sw = Stopwatch.StartNew();
        for (int rep = 0; rep < iterations; rep++)
            result = ConstantFoldingCompute(n);
        sw.Stop();
        double avgMs = sw.Elapsed.TotalMilliseconds / iterations;
        long expected = (long)n * 35;
        Console.WriteLine($"Benchmark: ConstantFolding (.NET 9 CLR)");
        Console.WriteLine($"  N = {n}");
        Console.WriteLine($"  Result = {result}");
        Console.WriteLine($"  Expected = {expected}");
        Console.WriteLine($"  Correct = {result == expected}");
        Console.WriteLine($"  Avg time = {avgMs:F3} ms");
    }

    // ── DeadCodeElim ────────────────────────────────────────────────────
    static long DeadCodeCompute(int n)
    {
        long sum = 0;
        for (int i = 0; i < n; i++)
        {
            sum += i;
            long dead = i * 17 + 42;
            dead = dead - dead;
        }
        return sum;
    }

    static void RunDeadCodeElim(int n, int iterations)
    {
        DeadCodeCompute(n); // warmup
        long result = 0;
        var sw = Stopwatch.StartNew();
        for (int rep = 0; rep < iterations; rep++)
            result = DeadCodeCompute(n);
        sw.Stop();
        double avgMs = sw.Elapsed.TotalMilliseconds / iterations;
        long expected = (long)n * (n - 1) / 2;
        Console.WriteLine($"Benchmark: DeadCodeElim (.NET 9 CLR)");
        Console.WriteLine($"  N = {n}");
        Console.WriteLine($"  Result = {result}");
        Console.WriteLine($"  Expected = {expected}");
        Console.WriteLine($"  Correct = {result == expected}");
        Console.WriteLine($"  Avg time = {avgMs:F3} ms");
    }
}
