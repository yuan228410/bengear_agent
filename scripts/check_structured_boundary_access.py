#!/usr/bin/env python3
"""Guard structured event/value boundary usage.

ExecutionValue and DomainEvent intentionally expose stable read/write helpers so
runtime, server, and UI adapters do not couple to their internal text/fields
storage. This check keeps production code on those APIs and allows direct
storage access only inside the defining headers and narrow serialization
boundaries.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

DEFAULT_ROOTS = ["include", "src", "tests"]
SOURCE_SUFFIXES = {".hpp", ".h", ".cpp", ".cc", ".cxx"}
ALLOWLIST = {
    Path("include/ben_gear/orchestration/result.hpp"),
    Path("include/ben_gear/domain/event.hpp"),
    Path("src/orchestration/serializer.cpp"),
}

FORBIDDEN_PATTERNS = [
    ("ExecutionValue", re.compile(r"\bpayload\.fields\b")),
    ("ExecutionValue", re.compile(r"\bpayload\.text\b")),
    ("ExecutionValue", re.compile(r"\bvalue\.fields\b")),
    ("ExecutionValue", re.compile(r"\bvalue\.text\b")),
    ("ExecutionValue", re.compile(r"\bExecutionValue\b.*\.fields\b")),
    ("DomainEvent", re.compile(r"\bdomain_event\.fields\b")),
    ("DomainEvent", re.compile(r"\bevent\.fields\b")),
    ("DomainEvent", re.compile(r"\bDomainEvent\b.*\.fields\b")),
    ("DomainEvent", re.compile(r"\bdomain_event\.(source|type|status|message)\b")),
    ("DomainEvent", re.compile(r"\bDomainEvent\b.*\.(source|type|status|message)\b")),
    ("DomainEvent", re.compile(r"\b\w*event\.(source|type|status)\s*(==|!=)")),
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
            for name, pattern in FORBIDDEN_PATTERNS:
                if pattern.search(line):
                    errors.append(f"{rel}:{lineno}: use {name} accessors instead: {line.strip()}")

    if errors:
        print("Structured boundary access check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("Structured boundary access check OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
