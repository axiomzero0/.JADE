#!/usr/bin/env python3
"""
tools/run_benchmark.py

Runs the .JADE and .NET benchmarks and generates a comparison report.

Usage:
    python3 tools/run_benchmark.py
"""

import json
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).parent.parent
DOTNET = "/home/z/my-project/.dotnet"
JADE_BENCH = REPO / "build" / "bin" / "jade_bench"
CSHARP_BENCH = REPO / "benchmarks" / "csharp"
CALLS = 100_000_000

def run_jade_bench():
    """Run the .JADE benchmark suite."""
    print("=== Running .JADE benchmarks ===")
    if not JADE_BENCH.exists():
        print("jade_bench not built. Building...")
        os.system(f"cd {REPO} && cmake --build build --target jade_bench -j 1")
    result = subprocess.run(
        [str(JADE_BENCH), str(CALLS)],
        capture_output=True, text=True, timeout=120
    )
    print(result.stdout)
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    return result.stdout

def run_dotnet_bench():
    """Run the .NET benchmark suite."""
    print("\n=== Running .NET 9 CLR benchmarks ===")
    env = os.environ.copy()
    env["PATH"] = f"{DOTNET}:{env.get('PATH', '')}"
    env["DOTNET_ROOT"] = DOTNET
    result = subprocess.run(
        ["dotnet", "run", "-c", "Release", "--", "All", str(CALLS), "5"],
        cwd=str(CSHARP_BENCH),
        capture_output=True, text=True, timeout=300, env=env
    )
    print(result.stdout)
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    return result.stdout

def parse_jade(output):
    """Parse .JADE benchmark output into a dict."""
    results = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 5:
            continue
        # Match lines that start with a benchmark name (not in notes section)
        name = parts[0]
        if name in ("ConstantFolding", "ArithmeticExpr", "ChainedExpr", "DeadCodeElim"):
            # The format is: <name> <result> <expected> <OK> <avg_ns> <total_ms>
            try:
                results[name] = {
                    "result": int(parts[1]),
                    "expected": int(parts[2]),
                    "correct": parts[3] == "Y",
                    "avg_ns": float(parts[4]),
                    "total_ms": float(parts[5]),
                }
            except (ValueError, IndexError):
                pass
    return results

def parse_dotnet(output):
    """Parse .NET benchmark output into a dict."""
    results = {}
    current_bench = None
    for line in output.splitlines():
        if line.startswith("Benchmark:"):
            name = line.split(":")[1].strip().split(" ")[0]
            current_bench = name
        elif "Avg time" in line and current_bench:
            parts = line.split("=")
            if len(parts) >= 2:
                ms_str = parts[-1].strip().replace("ms", "").strip()
                try:
                    ms = float(ms_str)
                    results[current_bench] = {"avg_ms": ms}
                except ValueError:
                    pass
            current_bench = None
    return results

def main():
    jade_out = run_jade_bench()
    dotnet_out = run_dotnet_bench()

    jade_results = parse_jade(jade_out)
    dotnet_results = parse_dotnet(dotnet_out)

    print("\n" + "=" * 80)
    print("BENCHMARK COMPARISON: .JADE vs .NET 9 CLR")
    print("=" * 80)
    print(f"\n{'Benchmark':<25} {'JADE (ns/call)':>15} {'.NET (ns/iter)':>15} {'Notes':>25}")
    print("-" * 80)

    if "ConstantFolding" in jade_results and "ConstantFolding" in dotnet_results:
        jade_ns = jade_results["ConstantFolding"]["avg_ns"]
        dotnet_ms = dotnet_results["ConstantFolding"]["avg_ms"]
        dotnet_ns = dotnet_ms * 1e6 / CALLS
        print(f"{'ConstantFolding':<25} {jade_ns:>15.2f} {dotnet_ns:>15.2f} {'JADE: per-call; .NET: loop iter':>25}")

    if "DeadCodeElim" in jade_results and "DeadCodeElim" in dotnet_results:
        jade_ns = jade_results["DeadCodeElim"]["avg_ns"]
        dotnet_ms = dotnet_results["DeadCodeElim"]["avg_ms"]
        dotnet_ns = dotnet_ms * 1e6 / CALLS
        print(f"{'DeadCodeElim':<25} {jade_ns:>15.2f} {dotnet_ns:>15.2f} {'JADE: per-call; .NET: loop iter':>25}")

    if "ArithmeticExpr" in jade_results and "ArithmeticLoop" in dotnet_results:
        jade_ns = jade_results["ArithmeticExpr"]["avg_ns"]
        dotnet_ms = dotnet_results["ArithmeticLoop"]["avg_ms"]
        dotnet_ns = dotnet_ms * 1e6 / CALLS
        print(f"{'Arithmetic (expr/loop)':<25} {jade_ns:>15.2f} {dotnet_ns:>15.2f} {'JADE: expr call; .NET: loop body':>25}")

    if "ChainedExpr" in jade_results:
        jade_ns = jade_results["ChainedExpr"]["avg_ns"]
        print(f"{'ChainedExpr':<25} {jade_ns:>15.2f} {'N/A':>15} {'JADE only':>25}")

    print("\n" + "-" * 80)
    print("Notes:")
    print("  - .NET benchmarks run 100M LOOP iterations of a function body.")
    print("    The loop is compiled to native code; no per-iteration call overhead.")
    print("  - .JADE benchmarks call the JIT-compiled function 100M times.")
    print("    Each call includes prologue/epilogue + function call overhead (~2ns).")
    print("  - .JADE's per-call overhead is dominated by the function call boundary,")
    print("    not the computation itself. With loop emission (planned), .JADE would")
    print("    match .NET's per-iteration cost (~0.3ns for folded constants).")
    print("=" * 80)

    # Save results for the benchmark record
    commit = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=str(REPO)).decode().strip()
    from datetime import datetime
    date = datetime.now().strftime("%Y-%m-%d")

    results_json = {
        "commit": commit,
        "date": date,
        "jade": jade_results,
        "dotnet": dotnet_results,
    }
    out_path = REPO / "build" / "bench-results.json"
    out_path.parent.mkdir(exist_ok=True)
    out_path.write_text(json.dumps(results_json, indent=2))
    print(f"\nResults saved to {out_path}")

    return 0

if __name__ == "__main__":
    sys.exit(main())
