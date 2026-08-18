#!/usr/bin/env python3
"""
tools/check_doc_headers.py

Validates that every .md file in docs/ has the mandatory YAML front-matter
header per the Documentation Standards §2 (Header & Metadata).

Mandatory fields:
  - title
  - status (must be one of: Draft, Stable, Deprecated)
  - owner
  - last_updated
  - related_rules (must be a list)

Usage:
    python3 tools/check_doc_headers.py
    (exit 0 = all docs pass; exit 1 = some docs missing headers)
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

VALID_STATUSES = {"Draft", "Stable", "Deprecated"}

# Pattern: YAML front-matter at the start of a file, delimited by --- lines.
FRONT_MATTER_RE = re.compile(r"^---\n(.*?)\n---\n", re.DOTALL)
FIELD_RE = re.compile(r'^(\w+):\s*(.*?)\s*$', re.MULTILINE)


def check_doc(path: Path) -> list[str]:
    """Return a list of errors for this doc. Empty list = no errors."""
    content = path.read_text()
    errors = []

    m = FRONT_MATTER_RE.match(content)
    if not m:
        return [f"{path}: missing YAML front-matter (expected at top of file)"]

    fm = m.group(1)
    fields = dict(FIELD_RE.findall(fm))

    # Required fields
    required = ["title", "status", "owner", "last_updated", "related_rules"]
    for req in required:
        if req not in fields:
            errors.append(f"{path}: missing required field '{req}'")

    # Status validation
    status = fields.get("status", "").strip().strip('"').strip("'")
    if status and status not in VALID_STATUSES:
        errors.append(f"{path}: invalid status '{status}' (must be one of {VALID_STATUSES})")

    # related_rules should be a YAML list (starts with [)
    related = fields.get("related_rules", "").strip()
    if related and not (related.startswith("[") and related.endswith("]")):
        errors.append(f"{path}: related_rules should be a YAML list '[...]'")

    return errors


def main() -> int:
    repo_root = Path(__file__).parent.parent
    docs_dir = repo_root / "docs"
    if not docs_dir.exists():
        print(f"ERROR: {docs_dir} does not exist", file=sys.stderr)
        return 2

    all_errors: list[str] = []
    checked = 0
    for md_path in sorted(docs_dir.rglob("*.md")):
        checked += 1
        errors = check_doc(md_path)
        all_errors.extend(errors)

    print(f"Checked {checked} markdown files in docs/")
    if all_errors:
        print(f"\nFAIL — {len(all_errors)} error(s):")
        for e in all_errors:
            print(f"  - {e}")
        return 1

    print("PASS — all docs have valid YAML front-matter.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
