#!/usr/bin/env python3

"""Compare firmware size reports and generate a markdown summary.

Usage:
    size_report.py <new_sizes.json> [--baseline <label>:<old_sizes.json>]...

Examples:
    # Compare PR against develop and master baselines
    size_report.py pr.json --baseline develop:develop.json --baseline master:master.json

    # Single baseline comparison
    size_report.py pr.json --baseline develop:develop.json

    # No baselines — shows sizes with blank delta columns
    size_report.py pr.json
"""

import argparse
import json
import sys


def load_sizes(path):
    with open(path) as f:
        return json.load(f)


def format_delta(n):
    """Format byte delta with sign and human-friendly suffix."""
    sign = "+" if n > 0 else ""
    if abs(n) >= 1024:
        return f"{sign}{n:,} ({sign}{n / 1024:.1f} KB)"
    return f"{sign}{n:,}"


def generate_markdown(new_sizes, baselines, top_n=5):
    """Generate a single table with current size and delta columns per baseline.

    baselines: list of (label, old_sizes_dict), may be empty
    """
    labels = [label for label, _ in baselines]

    # Build rows: (board, current_size, [(delta, abs_delta) per baseline])
    rows = []
    for board in sorted(new_sizes):
        current = new_sizes[board]
        deltas = []
        for _, old_sizes in baselines:
            old = old_sizes.get(board)
            if old is not None:
                d = current - old
                deltas.append((d, abs(d)))
            else:
                deltas.append((None, 0))
        # Sort key: max abs delta across baselines (biggest changes first)
        max_abs = max((ad for _, ad in deltas), default=0)
        rows.append((board, current, deltas, max_abs))

    rows.sort(key=lambda r: r[3], reverse=True)

    # Summary line
    sections = []
    summary_parts = [f"{len(new_sizes)} targets"]
    for i, (label, old_sizes) in enumerate(baselines):
        increases = sum(
            1 for _, _, deltas, _ in rows if deltas[i][0] is not None and deltas[i][0] > 0
        )
        decreases = sum(
            1 for _, _, deltas, _ in rows if deltas[i][0] is not None and deltas[i][0] < 0
        )
        net = sum(
            deltas[i][0] for _, _, deltas, _ in rows if deltas[i][0] is not None
        )
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
    header = "| Target | Size |"
    separator = "|--------|-----:|"
    for label in labels:
        header += f" vs `{label}` |"
        separator += "----------:|"
    sections.append(header)
    sections.append(separator)

    def format_row(board, current, deltas):
        row = f"| `{board}` | {current:,} |"
        for d, _ in deltas:
            if d is None:
                row += " |"
            elif d == 0:
                row += " 0 |"
            else:
                icon = "📈" if d > 0 else "📉"
                row += f" {icon} {format_delta(d)} |"
        return row

    # Top N rows always visible
    top = rows[:top_n]
    for board, current, deltas, _ in top:
        sections.append(format_row(board, current, deltas))

    # Remaining rows in expandable section
    rest = rows[top_n:]
    if rest:
        sections.append("")
        sections.append(
            f"<details><summary>Show {len(rest)} more target(s)</summary>\n"
        )
        sections.append(header)
        sections.append(separator)
        for board, current, deltas, _ in rest:
            sections.append(format_row(board, current, deltas))
        sections.append("\n</details>")

    sections.append("")
    return "\n".join(sections)


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
    args = parser.parse_args()

    new_sizes = load_sizes(args.new_sizes)

    baselines = []
    for b in args.baseline:
        if ":" not in b:
            print(f"Error: baseline must be LABEL:PATH, got '{b}'", file=sys.stderr)
            sys.exit(1)
        label, path = b.split(":", 1)
        baselines.append((label, load_sizes(path)))

    md = generate_markdown(new_sizes, baselines, top_n=args.top)
    print(md)


if __name__ == "__main__":
    main()
