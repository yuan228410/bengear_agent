#!/usr/bin/env python3
"""Guard ExecutionValue boundary usage.

ExecutionValue intentionally exposes stable read/write helpers so runtime,
server, and UI adapters do not couple to the internal text/fields storage.
This check keeps production code on that API and allows direct storage access
only inside the defining header and narrowly-scoped tests.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

DEFAULT_ROOTS = ["include", "src"]
SOURCE_SUFFIXES = {".hpp", ".h", ".cpp", ".cc", ".cxx"}
ALLOWLIST = {
    Path("include/ben_gear/orchestration/result.hpp"),
    Path("src/orchestration/serializer.cpp"),
}

FORBIDDEN_PATTERNS = [
    re.compile(r"\bpayload\.fields\b"),
    re.compile(r"\bpayload\.text\b"),
    re.compile(r"\bvalue\.fields\b"),
    re.compile(r"\bvalue\.text\b"),
    re.compile(r"\bExecutionValue\b.*\.fields\b"),
]


def iter_sources(root: Path, roots: list[str]):
    for rel_root in roots:
        base = root / rel_root
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                yield path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("paths", nargs="*", default=DEFAULT_ROOTS)
    args = parser.parse_args()

    root = args.root.resolve()
    errors: list[str] = []

    for path in iter_sources(root, args.paths):
        rel = path.relative_to(root)
        if rel in ALLOWLIST:
            continue
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except UnicodeDecodeError:
            continue
        for lineno, line in enumerate(lines, 1):
            for pattern in FORBIDDEN_PATTERNS:
                if pattern.search(line):
                    errors.append(f"{rel}:{lineno}: use ExecutionValue accessors instead: {line.strip()}")

    if errors:
        print("ExecutionValue access check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("ExecutionValue access check OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
