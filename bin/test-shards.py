#!/usr/bin/env python3
"""Emit the native-test CI matrix: one shard per matrix row, derived from test/.

Shards are safe to run in parallel because isolation is per suite, not per run: every suite gets
its own scratch $HOME via bin/pio-test-isolate.sh.

Two kinds of row come out:

  * general - a slice of the test_* tree under [env:coverage]. AREA_RULES place each suite, first
    match wins, unmatched to "misc". Areas over --max-suites split, smaller ones pack together.

  * fixed-env - one row per SPECIAL_ENVS entry, whose suite list is read from its test_filter in
    platformio.ini rather than restated here.

Usage:
  bin/test-shards.py                       # matrix JSON on stdout
  bin/test-shards.py --summary             # ... plus a human-readable table on stderr
  bin/test-shards.py --max-suites 8        # smaller shards, more of them
  bin/test-shards.py --seed 12345          # vary which suites share a shard

Exit: 0 ok, 2 on a malformed tree or a fixed env whose test_filter went missing.
"""

from __future__ import annotations

import argparse
import configparser
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# Ordered "area name" -> regex; first match wins. Extend an area by widening its regex, add an area
# by inserting a line. Anything unmatched lands in FALLBACK_AREA.
AREA_RULES = [
    ("admin", r"^test_(admin|pki)_"),
    ("crypto", r"^test_(crypto|packet_signing)$"),
    ("routing", r"^test_(mesh|nexthop|traceroute|hop|traffic|nodedb|warm)_"),
    ("position", r"^test_position_"),
    ("fuzz", r"^test_fuzz_"),
    ("packets", r"^test_(packet|transmit|meshpacket)_"),
    ("io", r"^test_(serial|stream|xmodem|http|mqtt)"),
]
FALLBACK_AREA = "misc"

# Envs that rebuild a fixed set of suites with different build flags. Their test_filter lives in
# the ini and is read from there.
NATIVE_INI = REPO / "variants" / "native" / "portduino" / "platformio.ini"
SPECIAL_ENVS = ["coverage-event-policy", "coverage-channel-table"]

# Suite names reach a shell as `-f <name>`. Constrained here, the one place the list is produced,
# so a creatively named directory cannot become shell text.
SUITE_RE = re.compile(r"^test_[A-Za-z0-9_]+$")


def discover_suites():
    """Every test_* directory directly under test/, sorted. The canonical set."""
    suites = sorted(
        p.name for p in (REPO / "test").iterdir() if p.is_dir() and p.name.startswith("test_")
    )
    bad = [s for s in suites if not SUITE_RE.match(s)]
    if bad:
        sys.exit(f"test-shards: refusing to shard, unusable suite name(s): {' '.join(bad)}")
    if not suites:
        sys.exit("test-shards: no test_* directories under test/ - the tree is not what it should be")
    return suites


def shuffle(seed, items):
    """Reorder via bin/lib/shuffle.sh, the one implementation of the seeded shuffle.

    Two copies of a Fisher-Yates would drift, and announce it as a replay reproducing a different
    arrangement. The repo path goes in as $1 so a checkout directory never becomes shell source.
    """
    script = 'source "$1"; shift; shuffle_suites "$@"'
    out = subprocess.run(
        ["bash", "-c", script, "_", str(REPO / "bin" / "lib" / "shuffle.sh"), seed, *items],
        capture_output=True,
        text=True,
        check=True,
    )
    return out.stdout.split()


def areas_of(suites):
    """Bucket suites into areas, preserving AREA_RULES order and putting misc last."""
    grouped = {name: [] for name, _ in AREA_RULES}
    grouped[FALLBACK_AREA] = []
    for suite in suites:
        area = next((name for name, rule in AREA_RULES if re.search(rule, suite)), FALLBACK_AREA)
        grouped[area].append(suite)
    return {name: members for name, members in grouped.items() if members}


def split(members, cap):
    """Split into the fewest chunks of at most `cap`, sized as evenly as the count allows.

    Wall clock is the slowest shard, so 11 suites at cap 10 becomes 6+5, not 10+1.
    """
    chunks = -(-len(members) // cap)  # ceil
    base, extra = divmod(len(members), chunks)
    out, start = [], 0
    for i in range(chunks):
        size = base + (1 if i < extra else 0)
        out.append(members[start : start + size])
        start += size
    return out


def pack(areas, cap):
    """Pack whole areas into the fewest shards of at most `cap`, keeping the loads even.

    Longest-processing-time-first: within 4/3 of optimal, and unlike first-fit it will not leave
    one shard holding a single two-suite area.
    """
    load = lambda b: sum(len(areas[a]) for a in b)  # noqa: E731
    ranked = sorted(areas, key=lambda a: len(areas[a]), reverse=True)
    # ceil(total / cap) is a lower bound, not a guarantee - whole areas do not divide, so three
    # areas of 6 at cap 10 would put 12 in one of two bins. Grow the count until every bin fits.
    for count in range(-(-sum(len(m) for m in areas.values()) // cap), len(areas) + 1):
        bins = [[] for _ in range(count)]
        for area in ranked:
            min(bins, key=load).append(area)
        if all(load(b) <= cap for b in bins):
            break
    # Report each shard's areas in declared order, so a name reads the same way the rules do.
    order = list(areas)
    return [sorted(b, key=order.index) for b in bins if b]


def build(areas, cap):
    """Lay the areas out into shards of at most `cap` suites. Returns (rows, suites placed)."""
    rows, placed, small = [], [], {}
    # Oversized areas become numbered shards of their own; what is left is packed together.
    for area, members in areas.items():
        if len(members) <= cap:
            small[area] = members
            continue
        for i, chunk in enumerate(split(members, cap), start=1):
            rows.append({"shard": f"{area}-{i}", "env": "coverage", "suites": " ".join(chunk)})
            placed += chunk
    for group in pack(small, cap):
        members = [suite for area in group for suite in small[area]]
        rows.append({"shard": "+".join(group), "env": "coverage", "suites": " ".join(members)})
        placed += members
    return rows, placed


def fixed_env_filter(env):
    """The suites [env:<env>] pins in its own test_filter."""
    # interpolation=None: platformio.ini interpolates with ${section.option}, not configparser's
    # %(name)s, so a bare % in any value elsewhere in the file would otherwise abort the parse.
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.read(NATIVE_INI, encoding="utf-8")
    section = f"env:{env}"
    if not parser.has_option(section, "test_filter"):
        sys.exit(
            f"test-shards: [{section}] in {NATIVE_INI.name} has no test_filter. It had one when this "
            f"matrix was written; either restore it or drop {env} from SPECIAL_ENVS - silently "
            f"emitting an empty filter would run every suite under the wrong build flags."
        )
    # test_filter accepts globs, and these tokens reach the same unquoted word-split and the same
    # attribution gate as discovered names. Hold them to SUITE_RE too, at the producer.
    names = parser.get(section, "test_filter").split()
    bad = [n for n in names if not SUITE_RE.match(n)]
    if bad:
        sys.exit(
            f"test-shards: [{section}] test_filter names something that is not a literal suite: "
            f"{' '.join(bad)}. The matrix and the attribution gate both need exact names."
        )
    return names


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "--max-suites",
        type=int,
        default=10,
        help="largest shard, in suites (default: 10). Lower is faster and costs more runners; the "
        "per-shard floor is checkout + toolchain + one src build, so past ~8 the fixed cost wins.",
    )
    ap.add_argument(
        "--max-shards",
        type=int,
        default=24,
        help="hard ceiling on matrix rows (default: 24). A budget, not a preference: --max-suites "
        "is raised until the matrix fits, so no branch can size the fan-out by adding directories.",
    )
    ap.add_argument(
        "--seed",
        default="",
        help="vary which suites share a shard. Co-location, not order - PlatformIO picks the order "
        "within a shard either way. Empty means the declared alphabetical arrangement.",
    )
    ap.add_argument("--summary", action="store_true", help="also print the shard table to stderr")
    args = ap.parse_args()

    if args.max_suites < 1:
        sys.exit("test-shards: --max-suites must be at least 1")
    if args.max_shards <= len(SPECIAL_ENVS):
        sys.exit(f"test-shards: --max-shards must leave room for the {len(SPECIAL_ENVS)} fixed envs")

    suites = discover_suites()
    areas = areas_of(suites)
    if args.seed:
        areas = {area: shuffle(args.seed, members) for area, members in areas.items()}

    # Shard size is a preference, shard count is a budget: without this a branch could size the
    # fan-out by adding directories. --max-suites gives way so the runner count stays bounded.
    cap = args.max_suites
    while True:
        rows, placed = build(areas, cap)
        if len(rows) + len(SPECIAL_ENVS) <= args.max_shards:
            break
        cap += 1
    if cap != args.max_suites:
        print(
            f"test-shards: {len(suites)} suites would need more than {args.max_shards} shards at "
            f"--max-suites {args.max_suites}; using {cap} per shard instead.",
            file=sys.stderr,
        )

    # Prove nothing fell out, rather than discovering an unrun suite from a coverage graph later.
    if sorted(placed) != suites:
        missing = sorted(set(suites) - set(placed))
        sys.exit(f"test-shards: {len(missing)} suite(s) reached no shard: {' '.join(missing)}")

    for env in SPECIAL_ENVS:
        rows.append(
            {
                "shard": env.removeprefix("coverage-"),
                "env": env,
                "suites": " ".join(fixed_env_filter(env)),
            }
        )

    # Exactly one shard writes the shared compiler cache: all of them compile the same src/ tree,
    # and letting each save would race for the key and store the same objects a dozen times.
    for row in rows:
        row["cache_writer"] = False
    rows[0]["cache_writer"] = True

    if args.summary:
        width = max(len(row["shard"]) for row in rows)
        for row in rows:
            count = len(row["suites"].split())
            print(
                f"  {row['shard']:<{width}}  {row['env']:<24} {count:>2} suite(s)", file=sys.stderr
            )
        print(
            f"  {len(rows)} shard(s), {len(suites)} suite(s) in test/, "
            f"largest shard {max(len(row['suites'].split()) for row in rows)}",
            file=sys.stderr,
        )

    print(json.dumps({"include": rows}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
