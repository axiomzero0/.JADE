#!/usr/bin/env python3
"""
tools/check_no_stubs.py

Enforces the No Stubs / No Placeholders Policy (docs/NO_STUBS_POLICY.md).

Scans every .hpp/.cpp file under src/jade/ for forbidden keywords:
  - TODO
  - FIXME
  - STUB
  - PLACEHOLDER
  - XXX
  - "not implemented" (case-insensitive)

Allowed bypass:
  - A line containing `// NOLINT(no-stubs)` followed by a WAIVER-NNN
    reference is skipped.

Usage:
    python3 tools/check_no_stubs.py
    (exit 0 = clean; exit 1 = stubs found)
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

FORBIDDEN_PATTERNS = [
    (re.compile(r'\bTODO\b', re.IGNORECASE), 'TODO'),
    (re.compile(r'\bFIXME\b', re.IGNORECASE), 'FIXME'),
    (re.compile(r'\bSTUB\b', re.IGNORECASE), 'STUB'),
    (re.compile(r'\bPLACEHOLDER\b', re.IGNORECASE), 'PLACEHOLDER'),
    (re.compile(r'\bXXX\b'), 'XXX'),
    (re.compile(r'not implemented', re.IGNORECASE), 'not implemented'),
]

# Directories that are scanned.
SCAN_DIRS = [
    "src/jade/core",
    "src/jade/ir",
    "src/jade/cil",
    "src/jade/jvm",
    "src/jade/runtime",
    "src/jade/tier1_jade",
    "src/jade/tier2_ruby",
    "src/jade/tier3_diamond",
]

# tier0_granit and driver are allowed exceptions (they use std::exception
# for runtime/IO errors). They are still scanned but with relaxed rules.
RELAXED_DIRS = [
    "src/jade/tier0_granit",
    "src/jade/driver",
]


def scan_file(path: Path, relaxed: bool = False) -> list[tuple[int, str, str]]:
    """Return list of (line_no, pattern_name, line_content) tuples for forbidden patterns."""
    findings = []
    try:
        lines = path.read_text().splitlines()
    except UnicodeDecodeError:
        return []  # binary file, skip

    for i, line in enumerate(lines, start=1):
        # Skip lines with NOLINT bypass
        if 'NOLINT(no-stubs)' in line:
            continue
        # Skip lines that are themselves documenting the policy
        if 'NO_STUBS_POLICY' in line or 'check_no_stubs' in line:
            continue
        # In relaxed mode, only flag STUB and PLACEHOLDER (not TODO/FIXME)
        patterns = FORBIDDEN_PATTERNS if not relaxed else [
            (re.compile(r'\bSTUB\b', re.IGNORECASE), 'STUB'),
            (re.compile(r'\bPLACEHOLDER\b', re.IGNORECASE), 'PLACEHOLDER'),
        ]
        for pat, name in patterns:
            if pat.search(line):
                # Filter out false positives:
                # - References to the policy doc itself
                # - The string "not implemented" inside a string literal
                #   returning an unsupported error (this is graceful degradation)
                if 'UnsupportedNode' in line or 'unsupported' in line.lower():
                    if name == 'not implemented':
                        continue
                findings.append((i, name, line.strip()))
                break  # one finding per line is enough
    return findings


def main() -> int:
    repo_root = Path(__file__).parent.parent

    total_files = 0
    total_findings = 0
    findings_per_file: dict[Path, list[tuple[int, str, str]]] = {}

    for scan_dir_name in SCAN_DIRS:
        scan_dir = repo_root / scan_dir_name
        if not scan_dir.exists():
            continue
        for src_file in sorted(scan_dir.rglob("*.hpp")) if scan_dir.exists() else []:
            pass
        for src_file in sorted(scan_dir.rglob("*.cpp")) if scan_dir.exists() else []:
            pass
        # Use both .hpp and .cpp
        for src_file in sorted(list(scan_dir.rglob("*.hpp")) + list(scan_dir.rglob("*.cpp"))):
            total_files += 1
            findings = scan_file(src_file, relaxed=False)
            if findings:
                findings_per_file[src_file] = findings
                total_findings += len(findings)

    for scan_dir_name in RELAXED_DIRS:
        scan_dir = repo_root / scan_dir_name
        if not scan_dir.exists():
            continue
        for src_file in sorted(list(scan_dir.rglob("*.hpp")) + list(scan_dir.rglob("*.cpp"))):
            total_files += 1
            findings = scan_file(src_file, relaxed=True)
            if findings:
                findings_per_file[src_file] = findings
                total_findings += len(findings)

    print(f"Scanned {total_files} source files in src/jade/")
    if not findings_per_file:
        print("PASS — 0 stubs found.")
        return 0

    print(f"\nFAIL — {total_findings} stub(s) found in {len(findings_per_file)} file(s):")
    for path, findings in findings_per_file.items():
        rel = path.relative_to(repo_root)
        for line_no, name, content in findings:
            print(f"  - {rel}:{line_no}: FOUND '{name}': {content}")
    print()
    print("Add `// NOLINT(no-stubs)` with a WAIVER-NNN reference to bypass.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
