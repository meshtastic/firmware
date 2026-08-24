#!/usr/bin/env python3
"""Post-process protoc-generated Python files to live under a local namespace.

Called by bin/regen-py-protos.sh. Walks the generated *_pb2.py files in the
target directory and rewrites every `meshtastic` reference (imports, dotted
attribute access) to use the new namespace (e.g., `meshtastic_v25`).

Why: the .proto files declare `package meshtastic;`, so protoc emits
`from meshtastic import mesh_pb2 as ...` lines. That would shadow the PyPI
`meshtastic` package which the meshtastic-mcp tooling depends on. Renaming
to a local namespace keeps both available.

Usage:
    _rewrite_proto_namespace.py <generated_dir> <new_namespace>
"""

from __future__ import annotations

import pathlib
import re
import sys


def rewrite(dir_path: pathlib.Path, new_ns: str) -> int:
    # Standard protoc import forms:
    #   from meshtastic.X_pb2 import ...     (rare, for direct symbol pulls)
    #   from meshtastic import X_pb2 as ...  (common, the cross-file ref)
    #   import meshtastic.X_pb2              (also possible)
    pattern_dotted_from = re.compile(r"^from meshtastic\.", re.MULTILINE)
    pattern_bare_from = re.compile(r"^from meshtastic import ", re.MULTILINE)
    pattern_dotted_import = re.compile(r"^import meshtastic\.", re.MULTILINE)

    count = 0
    for p in dir_path.glob("*.py"):
        text = p.read_text(encoding="utf-8")
        new = pattern_dotted_from.sub(f"from {new_ns}.", text)
        new = pattern_bare_from.sub(f"from {new_ns} import ", new)
        new = pattern_dotted_import.sub(f"import {new_ns}.", new)
        # NOTE: we deliberately leave `meshtastic/X.proto` source-filename
        # references inside descriptor strings alone. The descriptor pool is
        # keyed by source filename (independent of Python package layout), so
        # those don't collide with the PyPI package's descriptors.
        if new != text:
            p.write_text(new, encoding="utf-8")
            count += 1
    return count


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: _rewrite_proto_namespace.py <generated_dir> <new_namespace>", file=sys.stderr)
        return 2
    dir_path = pathlib.Path(argv[0])
    new_ns = argv[1]
    if not dir_path.is_dir():
        print(f"directory not found: {dir_path}", file=sys.stderr)
        return 2
    n = rewrite(dir_path, new_ns)
    print(f"rewrote {n} file(s) in {dir_path} → namespace {new_ns}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
