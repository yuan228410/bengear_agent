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

# Match both raw add_library/add_executable and the BenGear helper functions
# bengear_module()/bengear_interface() which wrap add_library.
TARGET_BLOCK_RE = re.compile(
    r"(?:add_(library|executable)|bengear_(module|interface))\s*\(\s*([^\s\)]+)(.*?)\)",
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
    # Generator expressions ($<...>) are not file paths.
    if token.startswith("$<"):
        return False
    if token.startswith("-"):
        return False
    return Path(token).suffix in SOURCE_SUFFIXES


def resolve_source(token: str, base_dir: Path) -> str:
    """Return a stable key for a source token for duplicate detection."""
    # Variable-prefixed (e.g. ${BENGEAR_ROOT}/src/...) or absolute paths are
    # already unique enough as written.
    if token.startswith("${") or Path(token).is_absolute():
        return token
    return str((base_dir / token).resolve())


def parse_targets(text: str, base_dir: Path | None = None):
    targets: dict[str, str] = {}
    target_sources: dict[str, list[str]] = {}

    for match in TARGET_BLOCK_RE.finditer(text):
        raw_kind, helper_kind, name, body = match.groups()
        # Skip CMake function/macro parameter placeholders (e.g. `${name}`).
        if "$" in name:
            continue
        tokens = [unquote(t) for t in tokenize(body)]
        if helper_kind:
            target_type = "INTERFACE" if helper_kind.lower() == "interface" else "STATIC"
        else:
            target_type = "EXECUTABLE" if raw_kind.lower() == "executable" else "STATIC"
            for token in tokens:
                if token in CMAKE_KEYWORDS:
                    target_type = token
                    break

        targets[name] = target_type
        target_sources[name] = [t for t in tokens if looks_like_source(t)]

    return targets, target_sources


def collect_cmake_files(root: Path) -> list[Path]:
    """Recursively collect project CMakeLists.txt, skipping third_party/build dirs."""
    files: list[Path] = []
    for path in sorted(root.rglob("CMakeLists.txt")):
        parts = path.parts
        if any(p in {"third_party", "build", "build-mingw", "webserver", "web"} for p in parts):
            continue
        files.append(path)
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "cmake",
        nargs="?",
        default=".",
        type=Path,
        help="Path to the project root or a CMakeLists.txt (default: current dir)",
    )
    args = parser.parse_args()

    root = args.cmake
    if root.is_file():
        cmake_files = [root]
    else:
        cmake_files = collect_cmake_files(root)

    if not cmake_files:
        print("No CMakeLists.txt found.", file=sys.stderr)
        return 1

    text = "\n".join(strip_comments(p.read_text(encoding="utf-8")) for p in cmake_files)
    targets: dict[str, str] = {}
    target_sources: dict[str, list[str]] = {}
    for path in cmake_files:
        file_text = strip_comments(path.read_text(encoding="utf-8"))
        file_targets, file_sources = parse_targets(file_text, path.parent)
        targets.update(file_targets)
        target_sources.update(file_sources)

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
    for path in cmake_files:
        file_text = strip_comments(path.read_text(encoding="utf-8"))
        _, file_sources = parse_targets(file_text, path.parent)
        for target, sources in file_sources.items():
            if target.startswith(("test_", "benchmark_")) or target in {"bengear_tests", "bengear_benchmarks"}:
                continue
            for source in sources:
                owners[resolve_source(source, path.parent)].append(target)

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
