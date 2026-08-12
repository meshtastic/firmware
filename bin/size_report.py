#!/usr/bin/env python3

"""Compare firmware size reports and generate a markdown summary.

Usage:
    size_report.py <new_sizes.json> [--baseline <label>:<old_sizes.json>]...
                   [--budgets <budgets.json>] [--enforce-budgets]

Examples:
    # Compare PR against develop and master baselines
    size_report.py pr.json --baseline develop:develop.json --baseline master:master.json

    # Single baseline comparison
    size_report.py pr.json --baseline develop:develop.json

    # No baselines - shows sizes with blank delta columns
    size_report.py pr.json

    # Render budget usage in the report (informational)
    size_report.py pr.json --budgets bin/ram_budgets.json

    # Enforce budgets: exit 1 when any env in the budgets file exceeds a limit
    size_report.py pr.json --budgets bin/ram_budgets.json --enforce-budgets

Sizes JSON schema (produced by bin/collect_sizes.py):
    {"<env>": {"flash_bytes": <int>, "ram_bytes": <int>}}
The legacy schema {"<env>": <flash_bytes>} (older baseline artifacts) is still
accepted; missing ram_bytes renders as "n/a".

Budgets JSON schema (bin/ram_budgets.json):
    {"<env>": {"ram_bytes": <budget>, "flash_bytes": <budget>}}
Keys starting with "_" are comments. Only envs present in the budgets file are
gated, and only for the metrics they list. ram_bytes is the static RAM
footprint (.data + .bss): on nRF52840 the heap arena is just the linker gap
after .bss, so every byte of static growth shrinks the usable heap 1:1 - which
is how the 2.8.0 heap regression shipped without CI noticing. Budgets are
raised deliberately (edit the JSON in the PR that needs the headroom and
justify it), never automatically.
"""

import argparse
import json
import sys

RAM_LABEL = "RAM (.data+.bss)"
FLASH_LABEL = "flash"


def load_sizes(path):
    with open(path) as f:
        return normalize_sizes(json.load(f))


def normalize_sizes(raw):
    """Normalize a sizes dict to {board: {"flash_bytes": int, "ram_bytes": int}}.

    Accepts both the current schema (dict values) and the legacy schema where
    each value was the flash size as a plain int. Unknown/malformed entries are
    dropped rather than crashing on old artifacts.
    """
    sizes = {}
    for board, val in raw.items():
        if isinstance(val, bool):
            continue
        if isinstance(val, int):
            sizes[board] = {"flash_bytes": val}
        elif isinstance(val, dict):
            entry = {}
            for key in ("flash_bytes", "ram_bytes"):
                if isinstance(val.get(key), int) and not isinstance(val[key], bool):
                    entry[key] = val[key]
            if entry:
                sizes[board] = entry
    return sizes


def format_delta(n):
    """Format byte delta with sign and human-friendly suffix."""
    sign = "+" if n > 0 else ""
    if abs(n) >= 1024:
        return f"{sign}{n:,} ({sign}{n / 1024:.1f} KB)"
    return f"{sign}{n:,}"


def compute_deltas(entry, baselines, key):
    """Per-baseline delta of entry[key], or None when either side is missing."""
    deltas = []
    current = entry.get(key)
    for _, old_sizes in baselines:
        old = (
            old_sizes.get(entry["_board"], {}).get(key) if current is not None else None
        )
        deltas.append(current - old if old is not None else None)
    return deltas


def generate_markdown(new_sizes, baselines, top_n=5):
    """Generate a single table with flash/RAM columns and deltas per baseline.

    baselines: list of (label, old_sizes_dict), may be empty
    """
    labels = [label for label, _ in baselines]

    # Build rows: (board, entry, flash_deltas, ram_deltas, max_abs_delta)
    rows = []
    for board in sorted(new_sizes):
        entry = dict(new_sizes[board])
        entry["_board"] = board
        flash_deltas = compute_deltas(entry, baselines, "flash_bytes")
        ram_deltas = compute_deltas(entry, baselines, "ram_bytes")
        max_abs = max(
            (abs(d) for d in flash_deltas + ram_deltas if d is not None), default=0
        )
        rows.append((board, entry, flash_deltas, ram_deltas, max_abs))

    rows.sort(key=lambda r: r[4], reverse=True)

    # Summary line (flash-based, matching the historical summary)
    sections = []
    summary_parts = [f"{len(new_sizes)} targets"]
    for i, (label, _) in enumerate(baselines):
        increases = sum(1 for _, _, fd, _, _ in rows if fd[i] is not None and fd[i] > 0)
        decreases = sum(1 for _, _, fd, _, _ in rows if fd[i] is not None and fd[i] < 0)
        net = sum(fd[i] for _, _, fd, _, _ in rows if fd[i] is not None)
        parts = []
        if increases:
            parts.append(f"{increases} increased")
        if decreases:
            parts.append(f"{decreases} decreased")
        if parts:
            parts.append(f"net {format_delta(net)}")
            summary_parts.append(f"vs `{label}`: {', '.join(parts)}")
        else:
            summary_parts.append(f"vs `{label}`: no changes")

    if not baselines:
        summary_parts.append("no baseline available yet")

    sections.append(f"**{' | '.join(summary_parts)}**\n")

    # Table header
    header = "| Target | Flash |"
    separator = "|--------|------:|"
    for label in labels:
        header += f" vs `{label}` |"
        separator += "----------:|"
    header += " RAM |"
    separator += "----:|"
    for label in labels:
        header += f" RAM vs `{label}` |"
        separator += "----------:|"
    sections.append(header)
    sections.append(separator)

    def format_delta_cell(d, missing=""):
        if d is None:
            return f" {missing} |".replace("  ", " ")
        if d == 0:
            return " 0 |"
        icon = "📈" if d > 0 else "📉"
        return f" {icon} {format_delta(d)} |"

    def format_row(board, entry, flash_deltas, ram_deltas):
        flash = entry.get("flash_bytes")
        ram = entry.get("ram_bytes")
        row = (
            f"| `{board}` | {flash:,} |"
            if flash is not None
            else f"| `{board}` | n/a |"
        )
        for d in flash_deltas:
            row += format_delta_cell(d)
        row += f" {ram:,} |" if ram is not None else " n/a |"
        for d in ram_deltas:
            # RAM data may be missing on one side (older artifacts): show n/a
            row += format_delta_cell(d, missing="n/a")
        return row

    # Top N rows always visible
    top = rows[:top_n]
    for board, entry, flash_deltas, ram_deltas, _ in top:
        sections.append(format_row(board, entry, flash_deltas, ram_deltas))

    # Remaining rows in expandable section
    rest = rows[top_n:]
    if rest:
        sections.append("")
        sections.append(
            f"<details><summary>Show {len(rest)} more target(s)</summary>\n"
        )
        sections.append(header)
        sections.append(separator)
        for board, entry, flash_deltas, ram_deltas, _ in rest:
            sections.append(format_row(board, entry, flash_deltas, ram_deltas))
        sections.append("\n</details>")

    sections.append("")
    return "\n".join(sections)


def load_budgets(path):
    """Load bin/ram_budgets.json, skipping "_comment"-style keys.

    Fails loudly on malformed entries: a typo'd budget (zero, negative, or
    non-integer) must not silently weaken or crash the gate.
    """
    with open(path) as f:
        raw = json.load(f)
    budgets = {
        env: limits
        for env, limits in raw.items()
        if not env.startswith("_") and isinstance(limits, dict)
    }
    for env, limits in budgets.items():
        for key in ("ram_bytes", "flash_bytes"):
            if key not in limits:
                continue
            budget = limits[key]
            if not isinstance(budget, int) or isinstance(budget, bool) or budget <= 0:
                print(
                    f"Error: invalid budget in {path}: {env}.{key} = {budget!r} "
                    "(must be a positive integer)",
                    file=sys.stderr,
                )
                sys.exit(1)
    return budgets


def check_budgets(new_sizes, budgets, fail_on_missing=False):
    """Compare measured sizes against per-env budgets.

    With fail_on_missing=False a budgeted env (or metric) absent from the
    measured sizes is reported as "n/a" but does not fail - right for the
    informational report. The enforcing gate passes fail_on_missing=True so
    missing data fails closed instead of trivially passing.
    Returns (violations, rows): violations is a list of human-readable failure
    strings; rows is a list of (env, metric_label, measured, budget, over)
    tuples for reporting, with measured=None when the data is unavailable.
    """
    violations = []
    rows = []
    for env in sorted(budgets):
        measured = new_sizes.get(env)
        for key, label in (("ram_bytes", RAM_LABEL), ("flash_bytes", FLASH_LABEL)):
            budget = budgets[env].get(key)
            if not isinstance(budget, int) or isinstance(budget, bool):
                continue
            value = measured.get(key) if measured is not None else None
            if value is None:
                rows.append((env, label, None, budget, False))
                if fail_on_missing:
                    reason = (
                        "env was not built in this run"
                        if measured is None
                        else "manifest is missing this metric"
                    )
                    violations.append(
                        f"{env} {label}: no measurement to check against the "
                        f"budget ({reason}); refusing to pass the gate blind"
                    )
                continue
            over = value > budget
            rows.append((env, label, value, budget, over))
            if over:
                violations.append(
                    f"{env} {label}: {value:,} bytes exceeds the budget of "
                    f"{budget:,} bytes (over by {value - budget:,})"
                )
    return violations, rows


def budget_markdown(rows):
    """Render budget usage as a markdown section for the PR comment."""
    if not rows:
        return ""
    lines = [
        "### Size budgets",
        "",
        "| Env | Metric | Measured | Budget | Used |",
        "|-----|--------|---------:|-------:|-----:|",
    ]
    for env, label, value, budget, over in rows:
        if value is None:
            lines.append(f"| `{env}` | {label} | n/a | {budget:,} | n/a |")
            continue
        # load_budgets() rejects non-positive budgets; guard anyway for direct callers
        pct = 100.0 * value / budget if budget > 0 else float("inf")
        status = f"{pct:.1f}%" + (" ❌ **OVER BUDGET**" if over else "")
        lines.append(f"| `{env}` | {label} | {value:,} | {budget:,} | {status} |")
    lines.append("")
    lines.append(
        "*Budgets live in `bin/ram_budgets.json` and are raised deliberately in "
        "the PR that needs the headroom - never automatically.*"
    )
    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Compare firmware size reports")
    parser.add_argument("new_sizes", help="Path to new sizes JSON")
    parser.add_argument(
        "--baseline",
        action="append",
        default=[],
        metavar="LABEL:PATH",
        help="Baseline to compare against (e.g. develop:develop.json)",
    )
    parser.add_argument(
        "--top",
        type=int,
        default=5,
        help="Number of top changes to show before collapsing (default: 5)",
    )
    parser.add_argument(
        "--budgets",
        metavar="PATH",
        help="Path to per-env budgets JSON (e.g. bin/ram_budgets.json)",
    )
    parser.add_argument(
        "--enforce-budgets",
        action="store_true",
        help="Exit 1 when any env exceeds its budget (requires --budgets)",
    )
    args = parser.parse_args()

    if args.enforce_budgets and not args.budgets:
        print("Error: --enforce-budgets requires --budgets", file=sys.stderr)
        sys.exit(1)

    new_sizes = load_sizes(args.new_sizes)

    # Silence output when no targets were built - repo maintainer choice.
    # Under enforcement that would be a silent pass, so fail closed instead.
    if not new_sizes:
        if args.enforce_budgets:
            print(
                f"Error: --enforce-budgets requested but no sizes were found in "
                f"{args.new_sizes}; refusing to pass the budget gate blind",
                file=sys.stderr,
            )
            sys.exit(1)
        return

    baselines = []
    for b in args.baseline:
        if ":" not in b:
            print(f"Error: baseline must be LABEL:PATH, got '{b}'", file=sys.stderr)
            sys.exit(1)
        label, path = b.split(":", 1)
        baselines.append((label, load_sizes(path)))

    md = generate_markdown(new_sizes, baselines, top_n=args.top)

    violations = []
    if args.budgets:
        budgets = load_budgets(args.budgets)
        violations, rows = check_budgets(
            new_sizes, budgets, fail_on_missing=args.enforce_budgets
        )
        budget_md = budget_markdown(rows)
        if budget_md:
            md += "\n" + budget_md

    print(md)

    if violations and args.enforce_budgets:
        print("\nRAM/flash budget check FAILED:", file=sys.stderr)
        for v in violations:
            print(f"  - {v}", file=sys.stderr)
        print(
            "\nOn nRF52840 the heap arena is the linker gap after .bss, so every\n"
            "byte of static RAM growth shrinks the usable heap 1:1. Budgets are\n"
            "raised deliberately, never automatically: if this growth is intended\n"
            "and reviewed, raise the env's limit in bin/ram_budgets.json in this\n"
            "same PR and justify the increase in the PR description.",
            file=sys.stderr,
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
