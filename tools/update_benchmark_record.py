#!/usr/bin/env python3
"""
tools/update_benchmark_record.py

Appends a new row to docs/BENCHMARK_RECORD.md after running benchmarks.

Usage:
    python3 tools/update_benchmark_record.py /tmp/bench-results-<commit-sha>.json

The script:
  1. Reads the benchmark results JSON.
  2. Computes the ratio vs the baseline.
  3. Appends a row to docs/BENCHMARK_RECORD.md.
  4. Exits with non-zero status if any ratio drops below 0.95 without a WAIVER flag.

JSON format:
    {
        "commit": "abc1234",
        "date": "2026-08-19",
        "results": [
            {
                "tier": "T3 DIAMOND",
                "benchmark": "Mandelbrot",
                "target": "C# (.NET 9)",
                "time_ms": 12.4,
                "alloc_mb": 0.0,
                "baseline_ms": 11.8,
                "waiver_id": null,
                "notes": "Initial PEA implementation"
            }
        ]
    }
"""

from __future__ import annotations

import json
import os
import sys
from datetime import datetime
from pathlib import Path

REGRESSION_THRESHOLD = 0.95  # anything below this is a regression


def append_record(record_path: Path, results: dict) -> int:
    """Append rows to the benchmark record. Returns 0 on success, 1 on regression."""
    commit = results.get("commit", "unknown")[:7]
    date = results.get("date", datetime.now().strftime("%Y-%m-%d"))

    # Read existing record
    content = record_path.read_text() if record_path.exists() else ""

    # Find the | Date | ... | table header
    lines = content.splitlines()
    table_start = -1
    for i, line in enumerate(lines):
        if line.strip().startswith("| Date | Commit Hash | Tier |"):
            table_start = i
            break

    if table_start == -1:
        print("ERROR: Could not find the benchmark table in the record file.", file=sys.stderr)
        return 2

    # Find the separator line (| :--- | :--- | ... |)
    separator_idx = -1
    for i in range(table_start + 1, len(lines)):
        if lines[i].strip().startswith("| :--- |"):
            separator_idx = i
            break

    if separator_idx == -1:
        print("ERROR: Could not find table separator.", file=sys.stderr)
        return 2

    # New rows go right after the separator
    new_rows = []
    has_regression = False
    for r in results.get("results", []):
        baseline_ms = r.get("baseline_ms", 0)
        time_ms = r.get("time_ms", 0)
        ratio = (time_ms / baseline_ms) if baseline_ms else 0
        waiver_id = r.get("waiver_id")
        if ratio < REGRESSION_THRESHOLD and not waiver_id:
            has_regression = True
            print(f"REGRESSION: {r['benchmark']} ({r['target']}) ratio={ratio:.2f} < {REGRESSION_THRESHOLD}",
                  file=sys.stderr)
        notes = r.get("notes", "")
        if waiver_id:
            notes = f"{waiver_id}: {notes}" if notes else waiver_id
        row = (
            f"| {date} | `{commit}` | {r['tier']} | {r['benchmark']} | "
            f"{r['target']} | {time_ms:.1f} | {r.get('alloc_mb', 0):.1f} | "
            f"{ratio:.2f}x | {notes} |"
        )
        new_rows.append(row)

    # Insert after separator
    for offset, row in enumerate(new_rows):
        lines.insert(separator_idx + 1 + offset, row)

    record_path.write_text("\n".join(lines) + "\n")
    print(f"Appended {len(new_rows)} rows to {record_path}")
    return 1 if has_regression else 0


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <bench-results.json>", file=sys.stderr)
        return 2

    results_file = Path(sys.argv[1])
    if not results_file.exists():
        print(f"ERROR: {results_file} does not exist", file=sys.stderr)
        return 2

    with open(results_file) as f:
        results = json.load(f)

    repo_root = Path(__file__).parent.parent
    record_path = repo_root / "docs" / "BENCHMARK_RECORD.md"
    return append_record(record_path, results)


if __name__ == "__main__":
    sys.exit(main())
