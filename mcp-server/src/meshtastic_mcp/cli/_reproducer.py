"""Reproducer bundle builder for ``meshtastic-mcp-test-tui``.

When the operator presses ``x`` on a failed test leaf, we package the
minimum viable failure context into a tarball under
``mcp-server/tests/reproducers/``:

::

    repro-<ts>-<short_nodeid>.tar.gz
      ├── README.md            human-readable overview
      ├── test_report.json     the failing TestReport event from reportlog
      ├── fwlog.jsonl          firmware log filtered to the failure window
      ├── devices.json         per-device device_info + lora config snapshot
      └── env.json             seed, run #, pytest version, platform, hostname

Separate module so the logic can be unit-tested without Textual. The
TUI glue is thin — one key binding calls :func:`build_reproducer_bundle`
with the focused test's state and shows the path in a modal.
"""

from __future__ import annotations

import io
import json
import pathlib
import platform
import re
import socket
import tarfile
import time
from dataclasses import dataclass
from typing import Any, Iterable


@dataclass
class ReproContext:
    """Everything :func:`build_reproducer_bundle` needs. Shaped to map
    cleanly onto the state the TUI already tracks — no extra data
    collection required at export time."""

    nodeid: str
    longrepr: str
    sections: list[tuple[str, str]]
    start_ts: float | None
    stop_ts: float | None
    seed: str
    run_number: int
    exit_code: int | None
    fwlog_path: pathlib.Path
    output_dir: pathlib.Path
    extra_device_rows: list[dict[str, Any]]  # [{role, port, info, ...}, ...]


def _short_nodeid(nodeid: str) -> str:
    """Collapse a pytest nodeid into a filename-safe slug (<= 60 chars)."""
    # Drop the file path prefix; keep test name + parametrization.
    tail = nodeid.split("::", 1)[-1] if "::" in nodeid else nodeid
    slug = re.sub(r"[^A-Za-z0-9_.\-]", "_", tail)
    return slug[:60].strip("_.-") or "test"


def _filtered_fwlog(
    fwlog_path: pathlib.Path,
    start_ts: float | None,
    stop_ts: float | None,
    *,
    pad_s: float = 5.0,
) -> bytes:
    """Return fwlog.jsonl lines whose ``ts`` lies in [start-pad, stop+pad]."""
    if not fwlog_path.is_file():
        return b""
    if start_ts is None or stop_ts is None:
        # Without a time window, include the whole file — rare; happens
        # when a test fails in setup before pytest emitted a start ts.
        try:
            return fwlog_path.read_bytes()
        except OSError:
            return b""
    lo, hi = start_ts - pad_s, stop_ts + pad_s
    out = io.BytesIO()
    try:
        with fwlog_path.open("r", encoding="utf-8") as fh:
            for line in fh:
                stripped = line.strip()
                if not stripped:
                    continue
                try:
                    record = json.loads(stripped)
                except json.JSONDecodeError:
                    continue
                ts = record.get("ts")
                if not isinstance(ts, (int, float)):
                    continue
                if lo <= ts <= hi:
                    out.write(line.encode("utf-8"))
    except OSError:
        return b""
    return out.getvalue()


def _readme(ctx: ReproContext) -> str:
    t = time.strftime("%Y-%m-%d %H:%M:%S %Z", time.localtime())
    return f"""# Reproducer bundle

Exported by `meshtastic-mcp-test-tui` on {t}.

## Failing test

- **nodeid:** `{ctx.nodeid}`
- **seed:** `{ctx.seed}`
- **run #:** {ctx.run_number}
- **suite exit code (at export time):** {ctx.exit_code if ctx.exit_code is not None else "in progress"}

## Files in this archive

| File | Contents |
|---|---|
| `test_report.json` | The pytest-reportlog `TestReport` event for the failing test — includes `longrepr`, captured `sections` (stdout/stderr/log), `duration`, `location`, `keywords`. |
| `fwlog.jsonl` | Firmware log lines (from `meshtastic.log.line` pubsub) filtered to [start−5s, stop+5s] around the test's run window. Each line is `{{ts, port, line}}`. |
| `devices.json` | Per-device snapshot at export time: `device_info` + `lora` config per detected role. |
| `env.json` | Python version, platform, hostname, seed, run number. |

## How to triage

1. Open `test_report.json` and read `longrepr` + `sections` — most failures explain themselves there.
2. If the failure is a mesh/telemetry assertion, `fwlog.jsonl` is where the answer usually lives. Grep for `Error=`, `NAK`, `PKI_UNKNOWN_PUBKEY`, `Skip send`, `Guru Meditation`, or the uptime timestamps around the assertion event.
3. Compare `devices.json` against the expected state (e.g. `num_nodes >= 2`, `primary_channel == "McpTest"`, `region == "US"`). If fields disagree with the seed-derived USERPREFS profile, the device probably wasn't baked with this session's profile.

## Reproducing locally

```bash
cd mcp-server
MESHTASTIC_MCP_SEED='{ctx.seed}' .venv/bin/pytest '{ctx.nodeid}' --tb=long -v
```
"""


def build_reproducer_bundle(ctx: ReproContext) -> pathlib.Path:
    """Build a tarball under ``ctx.output_dir`` and return its path.

    Parent dirs are created as needed. Errors during optional sections
    (devices, env) are swallowed — the bundle is still useful without
    them; refusing to export because the device poller had a hiccup
    would be worse than the export missing a file.
    """
    ctx.output_dir.mkdir(parents=True, exist_ok=True)
    ts = int(time.time())
    slug = _short_nodeid(ctx.nodeid)
    archive_path = ctx.output_dir / f"repro-{ts}-{slug}.tar.gz"

    with tarfile.open(archive_path, "w:gz") as tar:

        def _add(name: str, data: bytes) -> None:
            info = tarfile.TarInfo(name=name)
            info.size = len(data)
            info.mtime = ts
            tar.addfile(info, io.BytesIO(data))

        # README
        _add("README.md", _readme(ctx).encode("utf-8"))

        # test_report.json — reconstruct from the fields the TUI stashes.
        test_report = {
            "nodeid": ctx.nodeid,
            "outcome": "failed",
            "longrepr": ctx.longrepr,
            "sections": [list(s) for s in ctx.sections],
            "start": ctx.start_ts,
            "stop": ctx.stop_ts,
        }
        _add(
            "test_report.json",
            json.dumps(test_report, indent=2, default=str).encode("utf-8"),
        )

        # fwlog.jsonl (filtered)
        _add("fwlog.jsonl", _filtered_fwlog(ctx.fwlog_path, ctx.start_ts, ctx.stop_ts))

        # devices.json
        try:
            devices_payload = json.dumps(
                ctx.extra_device_rows or [], indent=2, default=str
            )
        except Exception:
            devices_payload = "[]"
        _add("devices.json", devices_payload.encode("utf-8"))

        # env.json
        try:
            from importlib.metadata import version as _pkg_version

            pytest_version = _pkg_version("pytest")
        except Exception:
            pytest_version = "unknown"
        env_payload = {
            "seed": ctx.seed,
            "run": ctx.run_number,
            "exit_code": ctx.exit_code,
            "export_ts": ts,
            "python": platform.python_version(),
            "pytest": pytest_version,
            "platform": f"{platform.system()} {platform.release()} {platform.machine()}",
            "hostname": socket.gethostname(),
        }
        _add("env.json", json.dumps(env_payload, indent=2).encode("utf-8"))

    return archive_path


def iter_entries(archive_path: pathlib.Path) -> Iterable[str]:
    """Yield member names — used by callers that want to confirm the bundle shape."""
    with tarfile.open(archive_path, "r:gz") as tar:
        for m in tar.getmembers():
            yield m.name
