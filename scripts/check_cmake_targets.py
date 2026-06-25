#!/usr/bin/env python3
"""Validate BenGear CMake target structure.

This is intentionally lightweight: it catches common mistakes from the target
modularization work without trying to become a full CMake parser.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".m",
    ".mm",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
}

TARGET_BLOCK_RE = re.compile(
    r"add_(library|executable)\s*\(\s*([^\s\)]+)(.*?)\)",
    re.IGNORECASE | re.DOTALL,
)

WARNINGS_RE = re.compile(r"bengear_apply_warnings\s*\(\s*([^\s\)]+)\s*\)")

CMAKE_KEYWORDS = {
    "STATIC",
    "SHARED",
    "MODULE",
    "OBJECT",
    "INTERFACE",
    "IMPORTED",
    "ALIAS",
    "EXCLUDE_FROM_ALL",
    "WIN32",
    "MACOSX_BUNDLE",
}


def strip_comments(text: str) -> str:
    lines: list[str] = []
    for line in text.splitlines():
        # CMake comments are line comments in this file. Keep this simple and
        # predictable rather than attempting quoted-string parsing.
        lines.append(line.split("#", 1)[0])
    return "\n".join(lines)


def tokenize(block: str) -> list[str]:
    return re.findall(r'"[^"]*"|\S+', block)


def unquote(token: str) -> str:
    if len(token) >= 2 and token[0] == '"' and token[-1] == '"':
        return token[1:-1]
    return token


def looks_like_source(token: str) -> bool:
    if not token or token in CMAKE_KEYWORDS:
        return False
    if token.startswith("$") or "$<" in token:
        return False
    if token.startswith("-"):
        return False
    return Path(token).suffix in SOURCE_SUFFIXES


def parse_targets(text: str):
    targets: dict[str, str] = {}
    target_sources: dict[str, list[str]] = {}

    for match in TARGET_BLOCK_RE.finditer(text):
        kind, name, body = match.groups()
        tokens = [unquote(t) for t in tokenize(body)]
        target_type = "EXECUTABLE" if kind.lower() == "executable" else "STATIC"
        for token in tokens:
            if token in CMAKE_KEYWORDS:
                target_type = token
                break

        targets[name] = target_type
        target_sources[name] = [t for t in tokens if looks_like_source(t)]

    return targets, target_sources


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "cmake",
        nargs="?",
        default="CMakeLists.txt",
        type=Path,
        help="Path to the top-level CMakeLists.txt",
    )
    args = parser.parse_args()

    raw = args.cmake.read_text(encoding="utf-8")
    text = strip_comments(raw)
    targets, target_sources = parse_targets(text)

    errors: list[str] = []

    # 1) INTERFACE targets must not receive private warning flags.
    for match in WARNINGS_RE.finditer(text):
        target = match.group(1)
        if targets.get(target) == "INTERFACE":
            errors.append(f"INTERFACE target has bengear_apply_warnings(): {target}")

    # 2) STATIC/OBJECT libraries should own at least one source file.
    for target, target_type in sorted(targets.items()):
        if target_type in {"STATIC", "OBJECT"} and not target_sources.get(target):
            errors.append(f"{target_type} target has no source files: {target}")

    # 3) Source files should not be compiled into multiple non-test/non-benchmark
    # targets. Duplicate compilation is easy to introduce during modular splits
    # and can hide ODR/linking mistakes.
    owners: dict[str, list[str]] = defaultdict(list)
    for target, sources in target_sources.items():
        if target.startswith(("test_", "benchmark_")) or target in {"bengear_tests", "bengear_benchmarks"}:
            continue
        for source in sources:
            owners[source].append(target)

    for source, source_owners in sorted(owners.items()):
        if len(source_owners) > 1:
            errors.append(f"source is attached to multiple targets: {source} -> {', '.join(source_owners)}")

    if errors:
        print("CMake target structure check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(f"CMake target structure OK ({len(targets)} targets checked).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
