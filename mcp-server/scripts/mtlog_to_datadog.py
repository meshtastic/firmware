#!/usr/bin/env python3
"""Forward selected recorder JSONL streams to Datadog.

Reads `.mtlog/logs.jsonl` and `.mtlog/telemetry.jsonl`, ships logs to the
Logs Intake API and telemetry numerics to the Metrics v2 series API.
Resumes from `.mtlog/.dd-cursor.json` so a daemon restart doesn't
duplicate rows already shipped from the current live files.

This forwarder does not currently backfill rotated `.jsonl.gz` archives.
If the recorder rotates before this process drains the live file, or the
forwarder is down across a rotation, those older rows are skipped.

Usage:
    DD_API_KEY=... ./scripts/mtlog_to_datadog.py --tail
    ./scripts/mtlog_to_datadog.py --once          # catch up + exit
    ./scripts/mtlog_to_datadog.py --since 3600   # backfill last hour from start

Default `DD_SITE` is `us5.datadoghq.com` — the team's Datadog instance.
Override via `DD_SITE=...` env var or `--site` flag for one-offs.

The forwarder is a separate process by design — a Datadog outage or
auth failure must not backpressure the recorder. We exit non-zero on
fatal config errors (missing API key) and keep retrying on transient
network/HTTP errors.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import sys
import time
from pathlib import Path
from typing import Any, Iterator

try:
    import requests
except ImportError:
    print(
        "requests is required. Install it in the mcp-server venv: "
        "uv pip install requests",
        file=sys.stderr,
    )
    sys.exit(2)


_DEFAULT_LOG_DIR = Path(__file__).resolve().parents[1] / ".mtlog"
_LOG_INTAKE_TPL = "https://http-intake.logs.{site}/api/v2/logs"
_METRICS_TPL = "https://api.{site}/api/v2/series"
_LOG_BATCH = 50
_METRICS_BATCH = 100
_MAX_RETRIES = 5
_RETRY_BASE_S = 1.5


# --- streaming JSONL with byte-position cursor -------------------------


class _StreamReader:
    """Reads a single rotating JSONL with cursor-based resume.

    This tails only the live `.jsonl` file. The recorder rotates files
    (live `.jsonl` → `.YYYYMMDD-HHMMSS-uuuuuu-NNNNN.jsonl.gz`), which means
    the live file shrinks abruptly. We detect that via inode change OR live
    size < cursor position, and reset the live-file cursor to 0.
    """

    def __init__(self, path: Path, cursor: dict[str, Any]):
        self.path = path
        self.cursor = cursor

    def _state(self) -> tuple[int, int]:
        """Return (inode, size) for the live file. (0, 0) if missing."""
        try:
            st = self.path.stat()
            return (st.st_ino, st.st_size)
        except FileNotFoundError:
            return (0, 0)

    def iter_new_records(self) -> Iterator[dict[str, Any]]:
        ino, size = self._state()
        last_ino = self.cursor.get("ino")
        last_pos = int(self.cursor.get("pos") or 0)
        if ino == 0:
            return
        if last_ino is not None and last_ino != ino:
            # Rotation happened. Start over.
            last_pos = 0
        if last_pos > size:
            # Live file truncated/shrunk under us — recorder rotated.
            last_pos = 0
        try:
            with self.path.open("r", encoding="utf-8") as fh:
                fh.seek(last_pos)
                for line in fh:
                    line = line.rstrip("\n")
                    if not line:
                        continue
                    try:
                        yield json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    last_pos = fh.tell()
        except FileNotFoundError:
            return
        self.cursor["ino"] = ino
        self.cursor["pos"] = last_pos


def _load_cursor(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError):
        return {}


def _save_cursor(path: Path, data: dict[str, Any]) -> None:
    tmp = path.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(data, separators=(",", ":")))
    tmp.replace(path)


# --- Datadog clients ---------------------------------------------------


class _DDSession:
    """Pool one HTTPS session, share retry logic."""

    def __init__(self, api_key: str, site: str, hostname: str) -> None:
        self.api_key = api_key
        self.site = site
        self.hostname = hostname
        self.session = requests.Session()
        self.session.headers.update(
            {
                "DD-API-KEY": api_key,
                "Content-Type": "application/json",
            }
        )

    def _post(self, url: str, payload: Any) -> bool:
        for attempt in range(_MAX_RETRIES):
            try:
                resp = self.session.post(url, json=payload, timeout=30)
            except requests.RequestException as e:
                _wait_retry(attempt, f"network error: {e}")
                continue
            if 200 <= resp.status_code < 300:
                return True
            if resp.status_code in (408, 429, 500, 502, 503, 504):
                _wait_retry(
                    attempt,
                    f"HTTP {resp.status_code} retrying",
                )
                continue
            print(
                f"datadog refused: {resp.status_code} {resp.text[:200]}",
                file=sys.stderr,
            )
            return False
        return False

    def send_logs(self, records: list[dict[str, Any]]) -> int:
        if not records:
            return 0
        url = _LOG_INTAKE_TPL.format(site=self.site)
        sent = 0
        for i in range(0, len(records), _LOG_BATCH):
            batch = records[i : i + _LOG_BATCH]
            if self._post(url, batch):
                sent += len(batch)
        return sent

    def send_metrics(self, series: list[dict[str, Any]]) -> int:
        if not series:
            return 0
        url = _METRICS_TPL.format(site=self.site)
        sent = 0
        for i in range(0, len(series), _METRICS_BATCH):
            batch = series[i : i + _METRICS_BATCH]
            if self._post(url, {"series": batch}):
                sent += len(batch)
        return sent


def _wait_retry(attempt: int, reason: str) -> None:
    wait = _RETRY_BASE_S * (2**attempt)
    print(
        f"  retry {attempt + 1}/{_MAX_RETRIES} in {wait:.1f}s ({reason})",
        file=sys.stderr,
    )
    time.sleep(wait)


# --- record → datadog payload ------------------------------------------


def _log_record_to_dd(rec: dict[str, Any], host: str) -> dict[str, Any]:
    line = rec.get("line") or ""
    tags = [
        f"role:{rec.get('role')}",
        f"port:{rec.get('port')}",
    ]
    level = rec.get("level")
    if level:
        tags.append(f"level:{level}")
    tag = rec.get("tag")
    if tag:
        tags.append(f"thread:{tag}")
    return {
        "ddsource": "meshtastic-firmware",
        "service": "meshtastic-firmware",
        "hostname": host,
        "message": line,
        "ddtags": ",".join(t for t in tags if t and "None" not in t),
        "timestamp": int((rec.get("ts") or time.time()) * 1000),
        "level": level,
    }


def _telemetry_record_to_metrics(
    rec: dict[str, Any], host: str
) -> list[dict[str, Any]]:
    fields = rec.get("fields") or {}
    if not isinstance(fields, dict):
        return []
    variant = rec.get("variant") or "unknown"
    ts = int(rec.get("ts") or time.time())
    out: list[dict[str, Any]] = []
    tags = []
    if rec.get("port"):
        tags.append(f"port:{rec['port']}")
    if rec.get("role"):
        tags.append(f"role:{rec['role']}")
    if rec.get("from_node"):
        tags.append(f"from_node:{rec['from_node']}")
    tags.append(f"variant:{variant}")
    for field, value in fields.items():
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            continue
        metric = f"mesh.{variant}.{_metric_safe(field)}"
        out.append(
            {
                "metric": metric,
                "type": 3,  # GAUGE
                "points": [{"timestamp": ts, "value": float(value)}],
                "tags": tags,
                "resources": [{"type": "host", "name": host}],
            }
        )
    return out


def _metric_safe(name: str) -> str:
    # Lowercase, replace non-alnum with underscore for safe metric names.
    return "".join(c.lower() if c.isalnum() else "_" for c in name)


# --- main loop ---------------------------------------------------------


def run(
    log_dir: Path,
    *,
    once: bool,
    since_seconds: float | None,
    poll_interval: float,
    dd: _DDSession,
) -> int:
    cursor_path = log_dir / ".dd-cursor.json"
    cursors = _load_cursor(cursor_path)

    # `--since` overrides cursor: rewind to (now-since) timestamp.
    # We can't seek by timestamp directly (cursor is byte position), so
    # we just reset cursors to 0 and let the time filter in iter_new
    # drop older records.
    cutoff_ts: float | None = None
    if since_seconds is not None:
        cursors = {}
        cutoff_ts = time.time() - since_seconds

    sent_total = {"logs": 0, "telemetry": 0}

    while True:
        # logs.jsonl → DD logs
        log_cursor = cursors.setdefault("logs", {})
        log_batch: list[dict[str, Any]] = []
        for rec in _StreamReader(log_dir / "logs.jsonl", log_cursor).iter_new_records():
            if cutoff_ts and (rec.get("ts") or 0) < cutoff_ts:
                continue
            log_batch.append(_log_record_to_dd(rec, dd.hostname))
        if log_batch:
            n = dd.send_logs(log_batch)
            sent_total["logs"] += n
            print(f"logs: sent {n}/{len(log_batch)}")

        # telemetry.jsonl → DD metrics
        telem_cursor = cursors.setdefault("telemetry", {})
        metric_series: list[dict[str, Any]] = []
        for rec in _StreamReader(
            log_dir / "telemetry.jsonl", telem_cursor
        ).iter_new_records():
            if cutoff_ts and (rec.get("ts") or 0) < cutoff_ts:
                continue
            metric_series.extend(_telemetry_record_to_metrics(rec, dd.hostname))
        if metric_series:
            n = dd.send_metrics(metric_series)
            sent_total["telemetry"] += n
            print(f"telemetry: sent {n}/{len(metric_series)} metric points")

        _save_cursor(cursor_path, cursors)

        if once:
            print(f"done. logs={sent_total['logs']} metrics={sent_total['telemetry']}")
            return 0
        time.sleep(poll_interval)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--log-dir",
        default=str(_DEFAULT_LOG_DIR),
        help="Path to .mtlog/ (default: mcp-server/.mtlog)",
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--once", action="store_true", help="Catch up then exit")
    mode.add_argument(
        "--tail",
        action="store_true",
        help="Daemon: poll forever (default)",
    )
    parser.add_argument(
        "--since",
        type=float,
        default=None,
        help="Backfill last N seconds. Resets cursor.",
    )
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=5.0,
        help="Seconds between tail polls (default 5)",
    )
    parser.add_argument(
        "--site",
        default=os.environ.get("DD_SITE", "us5.datadoghq.com"),
        help=(
            "Datadog site. Default is the team's instance (us5.datadoghq.com). "
            "Override via DD_SITE env var or this flag."
        ),
    )
    parser.add_argument(
        "--host",
        default=socket.gethostname(),
        help="Hostname tag (default: socket.gethostname())",
    )
    args = parser.parse_args(argv)

    api_key = os.environ.get("DD_API_KEY")
    if not api_key:
        print("DD_API_KEY env var required.", file=sys.stderr)
        return 2

    log_dir = Path(args.log_dir)
    if not log_dir.exists():
        print(
            f"log dir {log_dir} does not exist — start the mcp-server first.",
            file=sys.stderr,
        )
        return 2

    dd = _DDSession(api_key=api_key, site=args.site, hostname=args.host)
    once = args.once and not args.tail
    return run(
        log_dir,
        once=once,
        since_seconds=args.since,
        poll_interval=args.poll_interval,
        dd=dd,
    )


if __name__ == "__main__":
    sys.exit(main())
