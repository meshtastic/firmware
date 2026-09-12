#!/usr/bin/env python3
"""Attribute an ELF's flash back to the source files it was inlined from.

Usage:
    MESHTASTIC_DEBUG_INFO=1 pio run -e rak4631
    python bin/flash_attribution.py .pio/build/rak4631/firmware-*.elf

Why not bin/analyze_map.py: that reads the linker map, which answers "which object
file contributed this section". Once whole-image LTO has inlined across translation
units, that question stops matching the code - callees are folded into their callers
and the surviving symbol names no longer say where the bytes came from. On nRF52 the
single largest symbol is `setup` at ~8.9 KB, which is really dozens of inlined module
initialisers from all over src/.

So this walks the disassembly instead, asks addr2line for the *inline* stack at every
instruction, and charges each instruction's bytes to the innermost frame - the source
line the bytes actually came from. That surfaces costs no symbol-level view can, e.g.
~14 KB of C++ template instantiation spread across stl_vector.h/std_function.h/
stl_tree.h that is invisible to `nm --size-sort` because it has no symbol of its own.

Requires an image built with MESHTASTIC_DEBUG_INFO=1 (extra_scripts/debug_info.py);
without it every address resolves to "??". Debug info does not change image size.

For an interactive per-symbol inline tree, Wren6991/CodeSizer (CC0) reads the same
ELF and renders HTML.
"""

from __future__ import annotations

import argparse
import collections
import os
import re
import subprocess
import sys

# Some architectures space out halfwords/bytes, hence the tolerant byte-group match.
ADDR_LINE_RE = re.compile(
    r"^\s*([0-9a-fA-F]+):\s+([0-9a-fA-F]{2,}(?: [0-9a-fA-F]{2,})*)\s+"
)
ADDR2LINE_ADDR_RE = re.compile(r"^0x([0-9a-fA-F]+)\s*$")
DISCRIMINATOR_RE = re.compile(r"\s*\(discriminator \d+\)")


def human(n: int) -> str:
    return f"{n:,}"


def run(cmd: list[str], stdin_bytes: bytes | None = None) -> str:
    try:
        proc = subprocess.run(
            cmd,
            input=stdin_bytes,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )
    except FileNotFoundError:
        sys.exit(
            f"not found: {cmd[0]} (pass --cross-prefix, or put the toolchain on PATH)"
        )
    except subprocess.CalledProcessError as exc:
        sys.exit(f"{cmd[0]} failed: {exc.stderr.decode('utf-8', 'replace').strip()}")
    return proc.stdout.decode("utf-8", "replace")


def parse_instructions(disasm: str) -> list[tuple[int, int]]:
    """Parse `objdump -d` output into [(address, size_in_bytes)]."""
    out = []
    for line in disasm.splitlines():
        m = ADDR_LINE_RE.match(line)
        if m:
            out.append((int(m.group(1), 16), sum(c != " " for c in m.group(2)) // 2))
    return out


def addr2line_batch(
    prefix: str, elf: str, addresses: list[int]
) -> dict[int, list[tuple[str, str]]]:
    """Resolve every address in one call. Returns addr -> [(func, file)], innermost first."""
    stdin = "".join(f"{a:x}\n" for a in addresses).encode("ascii")
    out = run([prefix + "addr2line", "-fairC", "--exe", elf], stdin_bytes=stdin)
    lines = out.splitlines()
    stacks, i, n = {}, 0, len(lines)
    while i < n:
        m = ADDR2LINE_ADDR_RE.match(lines[i])
        if not m:
            i += 1
            continue
        addr, i, frames = int(m.group(1), 16), i + 1, []
        while i < n and not ADDR2LINE_ADDR_RE.match(lines[i]):
            name = lines[i]
            i += 1
            if i < n and not ADDR2LINE_ADDR_RE.match(lines[i]):
                fileline = lines[i]
                i += 1
            else:
                fileline = "??:0"
            path, _, _line = DISCRIMINATOR_RE.sub("", fileline).rpartition(":")
            frames.append((name, path))
        stacks[addr] = frames
    return stacks


def subsystem(path: str) -> str:
    """Roll a source path up into something you can act on."""
    if not path or path == "??":
        return "<unresolved>"
    p = path.replace("\\", "/")
    if "/libdeps/" in p:
        parts = p.split("/libdeps/")[-1].split("/")
        return "lib: " + (parts[1] if len(parts) > 1 else parts[0])
    if "framework-arduinoadafruitnrf52" in p or "/cores/nRF5/" in p:
        return "framework: arduino-nrf52"
    if "framework-arduinoespressif32" in p or "/esp-idf/" in p:
        return "framework: esp32"
    if "toolchain" in p or "/newlib" in p or "libstdc++" in p:
        return "toolchain (newlib/libstdc++)"
    if p.startswith("src/") or "/src/" in p:
        parts = p.split("src/", 1)[1].split("/")
        return "src/" + ("/".join(parts[:2]) if len(parts) > 2 else parts[0])
    return "other: " + p.split("/")[0]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("elf", help="ELF built with MESHTASTIC_DEBUG_INFO=1")
    ap.add_argument(
        "--cross-prefix",
        default="arm-none-eabi-",
        help="toolchain prefix (default: arm-none-eabi-)",
    )
    ap.add_argument(
        "--section",
        "-j",
        action="append",
        help="ELF section, repeatable (default: .text)",
    )
    ap.add_argument(
        "--top",
        type=int,
        default=30,
        help="how many source files to list (default: 30)",
    )
    args = ap.parse_args(argv)

    if not os.path.isfile(args.elf):
        ap.error(f"ELF not found: {args.elf}")
    sections = args.section or [".text"]

    cmd = [args.cross_prefix + "objdump", "-d"] + [f"--section={s}" for s in sections]
    instrs = parse_instructions(run(cmd + [args.elf]))
    if not instrs:
        sys.exit(
            f"no instructions found in {', '.join(sections)} - wrong section or wrong --cross-prefix?"
        )
    total = sum(size for _, size in instrs)

    stacks = addr2line_batch(args.cross_prefix, args.elf, [a for a, _ in instrs])

    by_file: collections.Counter = collections.Counter()
    by_subsystem: collections.Counter = collections.Counter()
    unresolved = 0
    for addr, size in instrs:
        frames = stacks.get(addr)
        path = (
            frames[0][1] if frames else "??"
        )  # innermost frame == where the bytes came from
        if not path or path == "??":
            path, unresolved = "??", unresolved + size
        by_file[path] += size
        by_subsystem[subsystem(path)] += size

    pct = 100.0 * unresolved / total
    print(
        f"{', '.join(sections)} = {human(total)} bytes   unresolved {human(unresolved)} ({pct:.1f}%)"
    )
    if pct > 90:
        print(
            "\n  NOTE: almost nothing resolved. Rebuild with MESHTASTIC_DEBUG_INFO=1 - and note that"
        )
        print(
            "  under -flto the debug flag is needed on the LINK line too (extra_scripts/debug_info.py)."
        )

    print("\n=== BY SUBSYSTEM ===")
    for name, size in by_subsystem.most_common():
        print(f"{human(size):>10}  {100.0 * size / total:5.1f}%  {name}")

    print(f"\n=== TOP {args.top} SOURCE FILES ===")
    home = os.path.expanduser("~")
    for path, size in by_file.most_common(args.top):
        shown = path.replace(os.getcwd() + "/", "").replace(home, "~")
        print(f"{human(size):>10}  {100.0 * size / total:5.1f}%  {shown}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
