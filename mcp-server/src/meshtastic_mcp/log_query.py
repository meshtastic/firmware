"""Read-side queries over the recorder's JSONL streams.

Pure functions over `mcp-server/.mtlog/`. Streaming JSONL reader: never
loads a whole file. Time-bound queries short-circuit as soon as `ts`
exceeds the requested end. The recorder writes monotonically, so a
forward scan is cheap; we don't need an index.

All time arguments accept:
  - epoch seconds (int/float)
  - relative strings: "-15m", "-2h", "-3d", "now"
  - ISO-ish absolute strings: "2026-05-07T14:30:00" (naive timestamps are
    treated as UTC)

Tools that return data ALWAYS cap their output (max_lines / max_points
/ max), and report whether more matched than was returned.
"""

from __future__ import annotations

import gzip
import json
import re
import statistics
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterator

from .recorder.recorder import get_recorder

_REL_RE = re.compile(r"^\s*-\s*(\d+(?:\.\d+)?)\s*([smhd])\s*$")
_REGEX_PREVIEW_MAX = 100
_REGEX_PREVIEW_TRUNCATE = 97


def _parse_time(value: Any, *, now: float | None = None) -> float:
    """Coerce to epoch seconds. Defaults `now` to `time.time()`."""
    if value is None:
        return time.time()
    if isinstance(value, (int, float)):
        return float(value)
    if not isinstance(value, str):
        raise ValueError(f"invalid time: {value!r}")
    s = value.strip().lower()
    if s in ("", "now"):
        return time.time() if now is None else now
    m = _REL_RE.match(s)
    if m:
        n = float(m.group(1))
        unit = m.group(2)
        secs = n * {"s": 1, "m": 60, "h": 3600, "d": 86400}[unit]
        base = time.time() if now is None else now
        return base - secs
    # Try ISO 8601. Accept naive (assume UTC) and Z-suffixed.
    try:
        if s.endswith("z"):
            s = s[:-1] + "+00:00"
        dt = datetime.fromisoformat(s)
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=timezone.utc)
        return dt.timestamp()
    except ValueError as e:
        raise ValueError(f"unparseable time: {value!r}") from e


def _iter_jsonl(path: Path, *, since: float, until: float) -> Iterator[dict[str, Any]]:
    """Stream records in chronological order: rotated archives first
    (oldest → newest by lex sort, which is chronological for our
    `YYYYMMDD-HHMMSS-uuuuuu-NNNNN` archive naming), then the live file
    last. The "keep last N" pop-front logic in the window queries
    relies on records arriving in time order across files.
    """
    files: list[Path] = []
    # Gzipped archives are named "<stem>.YYYYMMDD-HHMMSS-uuuuuu-NNNNN.jsonl.gz".
    for archive in sorted(path.parent.glob(f"{path.stem}.*.jsonl.gz")):
        files.append(archive)
    if path.exists():
        files.append(path)
    for f in files:
        opener = gzip.open if f.suffix == ".gz" else open
        try:
            with opener(f, "rt", encoding="utf-8") as fh:  # type: ignore[arg-type]
                for line in fh:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        rec = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    ts = rec.get("ts")
                    if not isinstance(ts, (int, float)):
                        continue
                    if ts < since:
                        continue
                    if ts > until:
                        # Records are append-monotonic within a file, so
                        # the rest of this file is also past `until`.
                        # Archives can still overlap each other, so only
                        # short-circuit this file, not the whole scan.
                        break
                    yield rec
        except (FileNotFoundError, OSError):
            continue


# -- queries ------------------------------------------------------------


def logs_window(
    start: Any = "-15m",
    end: Any = "now",
    *,
    grep: str | None = None,
    level: str | None = None,
    tag: str | None = None,
    port: str | None = None,
    max_lines: int = 200,
) -> dict[str, Any]:
    """Recent firmware log lines, filtered.

    `level` accepts a single level name or pipe-separated set
    ("WARN|ERROR|CRIT"). `grep` is a regex (Python re) over the raw
    `line` field. Returns the last `max_lines` matches.
    """
    s = _parse_time(start)
    e = _parse_time(end)
    levels = _split_set(level)
    if grep:
        try:
            grep_re = re.compile(grep)
        except re.error as exc:
            preview = (
                grep
                if len(grep) <= _REGEX_PREVIEW_MAX
                else f"{grep[:_REGEX_PREVIEW_TRUNCATE]}..."
            )
            raise ValueError(f"invalid grep regex {preview!r}: {exc}") from exc
    else:
        grep_re = None

    base = get_recorder().base_dir
    matched = 0
    out: list[dict[str, Any]] = []
    for rec in _iter_jsonl(base / "logs.jsonl", since=s, until=e):
        if levels and rec.get("level") not in levels:
            continue
        if tag and rec.get("tag") != tag:
            continue
        if port and rec.get("port") != port:
            continue
        if grep_re and not grep_re.search(rec.get("line") or ""):
            continue
        matched += 1
        out.append(rec)
        if len(out) > max_lines:
            out.pop(0)  # keep the most recent N
    return {
        "lines": out,
        "total_matched": matched,
        "dropped": max(0, matched - max_lines),
        "window": {"start": s, "end": e},
    }


def telemetry_timeline(
    window: Any = "1h",
    *,
    variant: str = "local",
    field: str = "free_heap",
    port: str | None = None,
    max_points: int = 200,
) -> dict[str, Any]:
    """Timeseries of one telemetry field, downsampled.

    `field` matches both the protobuf snake_case name (`free_heap`,
    `heap_free_bytes`, `battery_level`) and camelCase (`freeHeap`).
    Server-side bucket-mean downsamples to ≤ `max_points`. Returns
    `slope_per_min` (linear regression slope, units/min) so a leak
    detector can read one number.
    """
    end = time.time()
    if isinstance(window, (int, float)):
        # Numeric `window` is a duration in seconds — "last N seconds".
        # Without this branch, `_parse_time(-N)` would treat -N as an
        # absolute epoch timestamp (i.e., Jan 1 1970 minus N seconds),
        # producing a wildly negative `start` and matching nothing.
        start = end - float(window)
    elif isinstance(window, str) and not window.startswith("-"):
        # Bare string like "1h" is sugar for "-1h".
        start = _parse_time(f"-{window}", now=end)
    else:
        start = _parse_time(window, now=end)

    base = get_recorder().base_dir
    raw: list[tuple[float, float]] = []
    field_aliases = _field_aliases(field)
    for rec in _iter_jsonl(base / "telemetry.jsonl", since=start, until=end):
        if rec.get("variant") != variant:
            continue
        if port and rec.get("port") != port:
            continue
        fields = rec.get("fields") or {}
        value: Any = None
        for alias in field_aliases:
            if alias in fields:
                value = fields[alias]
                break
        if not isinstance(value, (int, float)):
            continue
        raw.append((float(rec["ts"]), float(value)))

    if not raw:
        return {
            "points": [],
            "samples": 0,
            "min": None,
            "max": None,
            "slope_per_min": None,
            "window": {"start": start, "end": end, "variant": variant, "field": field},
        }

    points = _downsample(raw, max_points=max_points)
    values = [v for _, v in raw]
    return {
        "points": [{"ts": ts, "value": v} for ts, v in points],
        "samples": len(raw),
        "min": min(values),
        "max": max(values),
        "slope_per_min": _slope_per_min(raw),
        "window": {"start": start, "end": end, "variant": variant, "field": field},
    }


def packets_window(
    start: Any = "-5m",
    end: Any = "now",
    *,
    portnum: str | None = None,
    from_node: str | None = None,
    to_node: str | None = None,
    max: int = 200,
) -> dict[str, Any]:
    s = _parse_time(start)
    e = _parse_time(end)
    portnums = _split_set(portnum)
    base = get_recorder().base_dir
    matched = 0
    out: list[dict[str, Any]] = []
    for rec in _iter_jsonl(base / "packets.jsonl", since=s, until=e):
        if portnums and rec.get("portnum") not in portnums:
            continue
        if from_node and str(rec.get("from_node")) != str(from_node):
            continue
        if to_node and str(rec.get("to_node")) != str(to_node):
            continue
        matched += 1
        out.append(rec)
        if len(out) > max:
            out.pop(0)
    return {
        "packets": out,
        "total_matched": matched,
        "dropped": matched - max if matched > max else 0,
        "window": {"start": s, "end": e},
    }


def events_window(
    start: Any = "-1h",
    end: Any = "now",
    *,
    kind: str | None = None,
    max: int = 200,
) -> dict[str, Any]:
    s = _parse_time(start)
    e = _parse_time(end)
    kinds = _split_set(kind)
    base = get_recorder().base_dir
    matched = 0
    out: list[dict[str, Any]] = []
    for rec in _iter_jsonl(base / "events.jsonl", since=s, until=e):
        if kinds and rec.get("kind") not in kinds:
            continue
        matched += 1
        out.append(rec)
        if len(out) > max:
            out.pop(0)
    return {
        "events": out,
        "total_matched": matched,
        "dropped": matched - max if matched > max else 0,
        "window": {"start": s, "end": e},
    }


def export(
    start: Any,
    end: Any,
    dest_dir: str,
    *,
    streams: list[str] | None = None,
) -> dict[str, Any]:
    """Bundle a slice of each requested stream into `dest_dir`.

    For a notebook, a bug report, or a Datadog backfill. Output files
    are uncompressed JSONL (callers gzip themselves if they want to).
    """
    s = _parse_time(start)
    e = _parse_time(end)
    selected = streams or ["logs", "telemetry", "packets", "events"]
    dest = Path(dest_dir)
    dest.mkdir(parents=True, exist_ok=True)

    base = get_recorder().base_dir
    paths: dict[str, str] = {}
    for stream in selected:
        src = base / f"{stream}.jsonl"
        if not src.exists() and not list(base.glob(f"{stream}.*.jsonl.gz")):
            continue
        out_path = dest / f"{stream}.jsonl"
        n = 0
        with out_path.open("w", encoding="utf-8") as fh:
            for rec in _iter_jsonl(src, since=s, until=e):
                fh.write(json.dumps(rec, separators=(",", ":")) + "\n")
                n += 1
        paths[stream] = str(out_path)
        paths[f"{stream}_count"] = str(n)
    return {"dest_dir": str(dest), "paths": paths, "window": {"start": s, "end": e}}


# -- helpers ------------------------------------------------------------


def _split_set(value: str | None) -> set[str] | None:
    if not value:
        return None
    return {v.strip() for v in value.split("|") if v.strip()}


def _field_aliases(field: str) -> list[str]:
    """Accept snake_case OR camelCase, plus a few legacy aliases."""
    snake = field
    camel = _snake_to_camel(field)
    aliases = {snake, camel}
    # Old protobuf fields (pre-LocalStats) used different names
    legacy = {
        "free_heap": ["free_heap", "freeHeap", "heap_free_bytes", "heapFreeBytes"],
        "heap_free_bytes": [
            "heap_free_bytes",
            "heapFreeBytes",
            "free_heap",
            "freeHeap",
        ],
        "total_heap": ["total_heap", "totalHeap", "heap_total_bytes", "heapTotalBytes"],
        "heap_total_bytes": [
            "heap_total_bytes",
            "heapTotalBytes",
            "total_heap",
            "totalHeap",
        ],
    }
    if field in legacy:
        aliases.update(legacy[field])
    return list(aliases)


def _snake_to_camel(name: str) -> str:
    parts = name.split("_")
    return parts[0] + "".join(p.title() for p in parts[1:])


def _downsample(
    points: list[tuple[float, float]], *, max_points: int
) -> list[tuple[float, float]]:
    if len(points) <= max_points:
        return points
    # Even-bucket mean. Preserves shape better than nth-sample picking.
    n = len(points)
    bucket = n / max_points
    out: list[tuple[float, float]] = []
    i = 0
    for k in range(max_points):
        end = int((k + 1) * bucket)
        end = min(end, n)
        if end <= i:
            continue
        chunk = points[i:end]
        ts = chunk[len(chunk) // 2][0]
        val = statistics.fmean(v for _, v in chunk)
        out.append((ts, val))
        i = end
    return out


def _slope_per_min(points: list[tuple[float, float]]) -> float | None:
    """Least-squares slope (units per minute). None if too few points."""
    if len(points) < 2:
        return None
    xs = [t for t, _ in points]
    ys = [v for _, v in points]
    n = len(xs)
    mean_x = sum(xs) / n
    mean_y = sum(ys) / n
    num = sum((xs[i] - mean_x) * (ys[i] - mean_y) for i in range(n))
    den = sum((x - mean_x) ** 2 for x in xs)
    if den == 0:
        return None
    slope_per_sec = num / den
    return slope_per_sec * 60.0
