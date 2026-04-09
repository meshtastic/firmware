#!/usr/bin/env python3
"""Summarise linker map output to highlight heavy object files and libraries.

Usage:
    python bin/analyze_map.py --map .pio/build/rak4631/output.map --top 20

The script parses GNU ld map files and aggregates section sizes per object file
and per archive/library, then prints sortable tables that make it easy to spot
modules worth trimming or hiding behind feature flags.
"""
from __future__ import annotations

import argparse
import collections
import os
import re
import sys
from typing import DefaultDict, Dict, Tuple


SECTION_LINE_RE = re.compile(r"^\s+(?P<section>\S+)\s+0x[0-9A-Fa-f]+\s+0x(?P<size>[0-9A-Fa-f]+)\s+(?P<object>.+)$")
ARCHIVE_MEMBER_RE = re.compile(r"^(?P<archive>.+)\((?P<object>[^)]+)\)$")


def human_size(num_bytes: int) -> str:
    """Return a friendly size string with one decimal place."""
    if num_bytes < 1024:
        return f"{num_bytes:,} B"
    num = float(num_bytes)
    for unit in ("KB", "MB", "GB"):
        num /= 1024.0
        if num < 1024.0:
            return f"{num:.1f} {unit}"
    return f"{num:.1f} TB"


def shorten_path(path: str, root: str) -> str:
    """Prefer repository-relative paths for readability."""
    path = path.strip()
    if not path:
        return path

    # Normalise Windows archives (backslashes) to POSIX style for consistency.
    path = path.replace("\\", "/")

    # Attempt to strip the root when an absolute path lives inside the repo.
    if os.path.isabs(path):
        try:
            rel = os.path.relpath(path, root)
            if not rel.startswith(".."):
                return rel
        except ValueError:
            # relpath can fail on mixed drives on Windows; fall back to basename.
            pass
    return path


def describe_object(raw_object: str, root: str) -> Tuple[str, str]:
    """Return a human friendly object label and the library it belongs to."""
    raw_object = raw_object.strip()
    lib_label = "[app]"
    match = ARCHIVE_MEMBER_RE.match(raw_object)
    if match:
        archive = shorten_path(match.group("archive"), root)
        obj = match.group("object")
        lib_label = os.path.basename(archive) or archive
        label = f"{archive}:{obj}"
    else:
        label = shorten_path(raw_object, root)
        # If the object lives under libs, hint at the containing directory.
        parent = os.path.basename(os.path.dirname(label))
        if parent:
            lib_label = parent
    return label, lib_label


def parse_map(map_path: str, repo_root: str) -> Tuple[Dict[str, int], Dict[str, int], Dict[str, Dict[str, int]]]:
    per_object: DefaultDict[str, int] = collections.defaultdict(int)
    per_library: DefaultDict[str, int] = collections.defaultdict(int)
    per_object_sections: DefaultDict[str, DefaultDict[str, int]] = collections.defaultdict(lambda: collections.defaultdict(int))

    try:
        with open(map_path, "r", encoding="utf-8", errors="ignore") as handle:
            for line in handle:
                match = SECTION_LINE_RE.match(line)
                if not match:
                    continue

                section = match.group("section")
                if section.startswith("*") or section in {"LOAD", "ORIGIN"}:
                    continue

                size = int(match.group("size"), 16)
                if size == 0:
                    continue

                obj_token = match.group("object").strip()
                if not obj_token or obj_token.startswith("*") or "load address" in obj_token:
                    continue

                label, lib_label = describe_object(obj_token, repo_root)
                per_object[label] += size
                per_library[lib_label] += size
                per_object_sections[label][section] += size
    except FileNotFoundError:
        raise SystemExit(f"error: map file '{map_path}' not found. Run a build first.")

    return per_object, per_library, per_object_sections


def format_section_breakdown(section_sizes: Dict[str, int], total: int, limit: int = 3) -> str:
    items = sorted(section_sizes.items(), key=lambda kv: kv[1], reverse=True)
    parts = []
    for section, size in items[:limit]:
        pct = (size / total) * 100 if total else 0
        parts.append(f"{section} {pct:.1f}%")
    if len(items) > limit:
        remainder = total - sum(size for _, size in items[:limit])
        pct = (remainder / total) * 100 if total else 0
        parts.append(f"other {pct:.1f}%")
    return ", ".join(parts)


def print_report(map_path: str, top_n: int, per_object: Dict[str, int], per_library: Dict[str, int], per_object_sections: Dict[str, Dict[str, int]]):
    total_bytes = sum(per_object.values())
    if total_bytes == 0:
        print("No section data found in map file.")
        return

    print(f"Map file: {map_path}")
    print(f"Accounted size: {human_size(total_bytes)} across {len(per_object)} object files\n")

    sorted_objects = sorted(per_object.items(), key=lambda kv: kv[1], reverse=True)
    print(f"Top {min(top_n, len(sorted_objects))} object files by linked size:")
    for idx, (obj, size) in enumerate(sorted_objects[:top_n], 1):
        pct = (size / total_bytes) * 100
        breakdown = format_section_breakdown(per_object_sections[obj], size)
        print(f"{idx:2}. {human_size(size):>9}  ({size:,} B, {pct:5.2f}% of linked size)")
        print(f"    {obj}")
        if breakdown:
            print(f"    sections: {breakdown}")
    print()

    sorted_libs = sorted(per_library.items(), key=lambda kv: kv[1], reverse=True)
    print(f"Top {min(top_n, len(sorted_libs))} libraries or source roots:")
    for idx, (lib, size) in enumerate(sorted_libs[:top_n], 1):
        pct = (size / total_bytes) * 100
        print(f"{idx:2}. {human_size(size):>9}  ({size:,} B, {pct:5.2f}% of linked size)  {lib}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Highlight heavy object files from a GNU ld map file.")
    parser.add_argument("--map", default=".pio/build/rak4631/output.map", help="Path to the map file (default: %(default)s)")
    parser.add_argument("--top", type=int, default=20, help="Number of entries to display per table (default: %(default)s)")
    args = parser.parse_args()

    map_path = os.path.abspath(args.map)
    repo_root = os.path.abspath(os.getcwd())

    per_object, per_library, per_object_sections = parse_map(map_path, repo_root)
    print_report(os.path.relpath(map_path, repo_root), args.top, per_object, per_library, per_object_sections)


if __name__ == "__main__":
    main()
