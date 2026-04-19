"""Textual TUI wrapping `mcp-server/run-tests.sh`.

Launch:  ``meshtastic-mcp-test-tui [pytest-args]``

The TUI *wraps* ``run-tests.sh``; it never replaces it. Same script, same
env-var resolution, same ``userPrefs.jsonc`` session fixture. Four data
sources drive live state:

1. ``tests/reportlog.jsonl`` — written by ``pytest-reportlog``. Tailed in a
   worker thread; each JSON line is published as a :class:`ReportLogEvent`
   message. This is the authoritative source for tree population + per-test
   outcome.
2. The pytest subprocess ``stdout`` + ``stderr`` streams — line-by-line,
   published as :class:`PytestLine` messages and rendered verbatim in the
   pytest pane.
3. ``tests/fwlog.jsonl`` — firmware log stream. Written by the
   ``_firmware_log_stream`` autouse session fixture in ``conftest.py``
   (mirrors every ``meshtastic.log.line`` pubsub event), tailed by the
   :class:`FirmwareLogTailer` worker, displayed in a wrap-enabled
   RichLog with cycleable port filter.
4. ``devices.list_devices()`` + ``info.device_info(port)`` — polled only at
   startup and again after ``RunFinished``. Device polling while pytest
   holds a SerialInterface would deadlock on the exclusive port lock; the
   existing ``hub_devices`` fixture is session-scoped so there is no safe
   "between tests" window. The header reflects this with a "(stale)"
   marker while the run is active.

Key bindings (see :class:`TestTuiApp.BINDINGS`):
    ``r`` re-run focused  ``f`` filter tree  ``d`` failure detail
    ``g`` open report.html  ``l`` cycle firmware-log port filter
    ``x`` export reproducer bundle  ``c`` tool-coverage panel
    ``q`` / Ctrl-C  graceful quit with SIGINT → SIGTERM → SIGKILL escalation

Shipped today (v1 + v2 slice): test tree + tier counters with progress bars,
pytest tail, live firmware log with port filter, device strip with
"currently running" status column, failure-detail modal, reproducer bundle
export (filters fwlog by test's start/stop timestamps), tool-coverage
modal, cross-run history sparkline in the header, clean SIGINT
propagation. Still open (see the plan file): mesh topology mini-diagram
and airtime / channel-utilization gauges.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import signal
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Any, Iterator

# ---------------------------------------------------------------------------
# Configuration constants
# ---------------------------------------------------------------------------

# Tier names that map nodeids like "tests/<tier>/..." to counter buckets.
# Order here == display order in the tier-counters table. Matches the order
# `pytest_collection_modifyitems` in `conftest.py` uses:
#   bake → unit → mesh → telemetry → monitor → fleet → admin → provisioning
# so the counters table reads top-to-bottom in execution order.
#
# "bake" is the synthetic tier for `tests/test_00_bake.py` — the file sits
# at the `tests/` root rather than under a tier subdirectory, so without
# this mapping `_tier_of_nodeid` would return "other" and the bake outcomes
# would be silently dropped from both the tier table and the history
# record (which sums tier counters to compute passed/failed/skipped).
TIERS = (
    "bake",
    "unit",
    "mesh",
    "telemetry",
    "monitor",
    "fleet",
    "admin",
    "provisioning",
)

# Relative paths from the mcp-server root.
_REPORTLOG_RELATIVE = "tests/reportlog.jsonl"
_FWLOG_RELATIVE = "tests/fwlog.jsonl"
# pio / esptool / nrfutil / picotool tee subprocess output here when
# `MESHTASTIC_MCP_FLASH_LOG` is set (see `pio._run_capturing`). run-tests.sh
# sets that env var; the TUI also sets it for direct `_spawn_pytest` calls
# so `r`-key re-runs that skip the wrapper still get tee'd output.
_FLASHLOG_RELATIVE = "tests/flash.log"
_REPORT_HTML_RELATIVE = "tests/report.html"
_TOOL_COVERAGE_RELATIVE = "tests/tool_coverage.json"
_HISTORY_RELATIVE = "tests/.history/runs.jsonl"
_REPRODUCERS_RELATIVE = "tests/reproducers"
_RUN_TESTS_RELATIVE = "run-tests.sh"
_RUN_COUNTER_RELATIVE = "tests/.tui-runs"

# Graceful-shutdown budgets (seconds) for the pytest subprocess when the
# user hits `q`. Matches what the existing CLI's atexit + userprefs sidecar
# self-heal expects.
_SIGINT_GRACE_S = 5.0
_SIGTERM_GRACE_S = 5.0


# ---------------------------------------------------------------------------
# Path resolution
# ---------------------------------------------------------------------------


def _mcp_server_root() -> pathlib.Path:
    """Locate the mcp-server directory (the one containing run-tests.sh)."""
    here = pathlib.Path(__file__).resolve()
    # Walk up until we find pyproject.toml with a matching project name, or
    # default to the three-up ancestor (src/meshtastic_mcp/cli/test_tui.py →
    # .../mcp-server). The walk-up protects against unusual checkouts.
    for parent in (here.parent, *here.parents):
        if (parent / "pyproject.toml").is_file() and (
            parent / "run-tests.sh"
        ).is_file():
            return parent
    return here.parents[3]


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------


@dataclass
class LeafReport:
    """Per-test state drawn from reportlog events.

    Outcomes mirror pytest's: "passed" | "failed" | "skipped" | "running".
    """

    nodeid: str
    tier: str
    outcome: str = "pending"
    duration_s: float = 0.0
    longrepr: str = ""
    # Captured stdout / stderr / firmware-log sections from the test's
    # `TestReport.sections` — shown in the failure-detail modal.
    sections: list[tuple[str, str]] = field(default_factory=list)
    # Wall-clock start/stop from the TestReport event. Used by the
    # reproducer exporter (`x`) to filter `tests/fwlog.jsonl` down to
    # just the lines around the failure window.
    start_ts: float | None = None
    stop_ts: float | None = None


@dataclass
class TierCounters:
    tier: str
    passed: int = 0
    failed: int = 0
    skipped: int = 0
    running: int = 0
    remaining: int = 0


@dataclass
class DeviceRow:
    role: str | None
    port: str
    vid: str
    pid: str
    description: str
    # Populated from info.device_info when available; empty dict when we
    # haven't queried (or when the poller is paused).
    info: dict[str, Any] = field(default_factory=dict)


@dataclass
class State:
    """Shared state owned by the App; written by workers under `lock`.

    UI code reads via Textual Message handlers which run on the UI thread
    in the order workers called `post_message` — so reads don't need the
    lock themselves.
    """

    lock: threading.Lock = field(default_factory=threading.Lock)
    tiers: dict[str, TierCounters] = field(
        default_factory=lambda: {t: TierCounters(tier=t) for t in TIERS}
    )
    leaves: dict[str, LeafReport] = field(default_factory=dict)
    # Ordered list of nodeids in the order they were first seen — lets us
    # rebuild the tree deterministically.
    nodeid_order: list[str] = field(default_factory=list)
    devices: list[DeviceRow] = field(default_factory=list)
    run_active: bool = False
    exit_code: int | None = None
    # nodeid of the currently-running test. Set on `when="setup"` +
    # outcome="passed" (body about to execute); cleared on `when="call"`
    # (any outcome) or on `when="setup"` + outcome="failed" (no body
    # window). Drives the device-table "Status" column so the operator
    # can see which test is touching a given device right now.
    running_nodeid: str | None = None
    # `time.monotonic()` captured when `running_nodeid` was set. Surfaced
    # as live-updating elapsed-time ("RUNNING: test_bake_nrf52 (1:23)") so
    # an operator staring at a ~3 min `test_00_bake` or a `mesh_formation`
    # with a 60 s ceiling has concrete evidence the test isn't stuck.
    running_started_at: float | None = None


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _tier_of_nodeid(nodeid: str) -> str:
    """Map a pytest nodeid to its tier bucket. Unknown → 'other'.

    `tests/test_00_bake.py::...` is special-cased to the synthetic `bake`
    tier — it's a top-level file (no tier subdirectory) so the generic
    "second path segment" logic would miss it and route the bake outcomes
    into the non-existent `other` bucket.
    """
    parts = nodeid.split("/", 2)
    if len(parts) >= 2 and parts[0] == "tests":
        # Bake file sits at `tests/test_00_bake.py` — dedicated bucket.
        if parts[1].startswith("test_00_bake"):
            return "bake"
        candidate = parts[1]
        if candidate in TIERS:
            return candidate
    return "other"


def _file_of_nodeid(nodeid: str) -> str:
    """Extract the test file name (e.g. 'test_boards.py') from a nodeid."""
    left = nodeid.split("::", 1)[0]
    return left.rsplit("/", 1)[-1]


def _testname_of_nodeid(nodeid: str) -> str:
    """Extract the 'test_foo[param]' suffix from a nodeid, or the full thing."""
    if "::" in nodeid:
        return nodeid.split("::", 1)[1]
    return nodeid


def _roles_from_nodeid(nodeid: str) -> set[str]:
    """Infer which device roles a parametrized test touches.

    Patterns we recognize (from the existing ``conftest.py`` parametrization
    in ``pytest_generate_tests``):

    - ``test_foo[nrf52]``            → {"nrf52"}           (baked_single)
    - ``test_foo[nrf52->esp32s3]``   → {"nrf52", "esp32s3"} (mesh_pair)

    Unparametrized tests (no bracket) return an empty set — the caller
    should fall back to "this test involves ALL detected devices" rather
    than pretending it touches none.
    """
    if "[" not in nodeid or not nodeid.endswith("]"):
        return set()
    try:
        inner = nodeid.rsplit("[", 1)[1][:-1]
    except Exception:
        return set()
    # Split on "->" for directed mesh pairs; otherwise treat as single role.
    parts = [p.strip() for p in inner.split("->")] if "->" in inner else [inner.strip()]
    return {p for p in parts if p}


def _parse_events(path: pathlib.Path) -> Iterator[dict[str, Any]]:
    """Yield parsed JSON dicts from a reportlog file, skipping malformed lines.

    Used for smoke-testing the parser against a finished file; the live
    worker has its own tail loop.
    """
    if not path.is_file():
        return
    with path.open("r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError:
                continue


def _load_run_number(counter_path: pathlib.Path) -> int:
    """Bump + persist a monotonic run counter used in the TUI header."""
    try:
        n = int(counter_path.read_text().strip())
    except Exception:
        n = 0
    n += 1
    try:
        counter_path.parent.mkdir(parents=True, exist_ok=True)
        counter_path.write_text(str(n))
    except Exception:
        # Non-fatal: the counter is cosmetic.
        pass
    return n


def _resolve_seed() -> str:
    """Mirror the default-seed resolution from run-tests.sh.

    Operator can override via MESHTASTIC_MCP_SEED. Matches the
    per-user/per-host default so repeated invocations land on the same PSK
    (makes --assume-baked valid across invocations).
    """
    if explicit := os.environ.get("MESHTASTIC_MCP_SEED"):
        return explicit
    try:
        who = os.environ.get("USER") or os.environ.get("LOGNAME") or "anon"
    except Exception:
        who = "anon"
    try:
        import socket

        host = socket.gethostname().split(".", 1)[0]
    except Exception:
        host = "host"
    return f"mcp-{who}-{host}"


def _format_duration(seconds: float) -> str:
    if seconds < 60:
        return f"{seconds:5.1f}s"
    m, s = divmod(int(seconds), 60)
    return f"{m:d}:{s:02d}"


# ---------------------------------------------------------------------------
# Textual imports (lazy — only when main() runs, so `_parse_events` can be
# imported by smoke tests without requiring textual installed in every env)
# ---------------------------------------------------------------------------


def _import_textual() -> Any:
    """Return a namespace carrying every Textual class we use.

    Deferred import keeps `_parse_events` + `_tier_of_nodeid` importable
    from tests / smoke scripts without pulling in the UI stack.
    """
    import textual
    from textual.app import App, ComposeResult
    from textual.binding import Binding
    from textual.containers import Horizontal, Vertical
    from textual.message import Message
    from textual.screen import ModalScreen
    from textual.widgets import DataTable, Footer, Input, RichLog, Static, Tree

    ns = argparse.Namespace()
    ns.App = App
    ns.Binding = Binding
    ns.ComposeResult = ComposeResult
    ns.DataTable = DataTable
    ns.Footer = Footer
    ns.Horizontal = Horizontal
    ns.Input = Input
    ns.Message = Message
    ns.ModalScreen = ModalScreen
    ns.RichLog = RichLog
    ns.Static = Static
    ns.Tree = Tree
    ns.Vertical = Vertical
    ns.textual = textual
    return ns


# ---------------------------------------------------------------------------
# main() — the important scaffolding lives here so that when we bail out
# before entering the Textual event loop (missing terminal, --help, etc.)
# nothing has grabbed the screen yet.
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    """Entry point for `meshtastic-mcp-test-tui`."""
    argv = list(argv if argv is not None else sys.argv[1:])

    parser = argparse.ArgumentParser(
        prog="meshtastic-mcp-test-tui",
        description=(
            "Live Textual TUI wrapping mcp-server/run-tests.sh. "
            "Passes any unrecognized arguments through to pytest."
        ),
        allow_abbrev=False,
    )
    parser.add_argument(
        "--no-tui",
        action="store_true",
        help=(
            "Skip the TUI and exec run-tests.sh directly. Useful as a health "
            "check that the wrapper argv+env resolution is working."
        ),
    )
    args, pytest_args = parser.parse_known_args(argv)

    root = _mcp_server_root()
    run_tests = root / _RUN_TESTS_RELATIVE
    reportlog = root / _REPORTLOG_RELATIVE
    fwlog = root / _FWLOG_RELATIVE
    flashlog = root / _FLASHLOG_RELATIVE
    counter = root / _RUN_COUNTER_RELATIVE

    if not run_tests.is_file():
        print(
            f"error: could not locate {_RUN_TESTS_RELATIVE} relative to "
            f"{root}. Is this the mcp-server checkout?",
            file=sys.stderr,
        )
        return 2

    # Always clear stale log files before launching pytest. The TUI's tail
    # workers race pytest file-creation; starting from a known-empty state
    # avoids mid-line-decode confusion from the prior run. The fwlog session
    # fixture also truncates on its end, and run-tests.sh truncates the
    # flashlog — triple-truncate is deliberate (whichever side creates the
    # file first, it starts empty).
    for p in (reportlog, fwlog, flashlog):
        try:
            p.unlink(missing_ok=True)
        except Exception:
            pass

    # Compute + persist the run counter for the header (cosmetic).
    run_number = _load_run_number(counter)
    seed = _resolve_seed()
    # Export the seed so the subprocess inherits the SAME value the TUI
    # displays. run-tests.sh computes its own fallback if unset, and we'd
    # end up with a header / wrapper-header mismatch if we let that happen.
    os.environ.setdefault("MESHTASTIC_MCP_SEED", seed)
    # Turn on subprocess-output tee'ing so `pio._run_capturing` writes each
    # line of pio / esptool / nrfutil / picotool output to `tests/flash.log`
    # as it arrives. The TUI tails that file and routes each line to the
    # pytest pane so the operator sees live flash progress during long
    # `pio run -t upload` / `esptool erase_flash` operations. run-tests.sh
    # also sets this when invoked directly — `setdefault` so the wrapper's
    # value wins when present.
    os.environ.setdefault("MESHTASTIC_MCP_FLASH_LOG", str(flashlog))

    # --no-tui: exec run-tests.sh directly. Useful for diagnosing wrapper
    # env / argv handling without getting into Textual's alternate screen.
    if args.no_tui:
        cmd = [str(run_tests), *pytest_args]
        os.execv(str(run_tests), cmd)  # noqa: S606 — intentional

    # Textual UI import is deferred so `--help` and `--no-tui` do not pay
    # the ~40 MB startup cost.
    try:
        tx = _import_textual()
    except ImportError as exc:
        print(
            f"error: textual is not installed ({exc}). Install with: "
            f"pip install -e '.[test]'",
            file=sys.stderr,
        )
        return 2

    # Narrow-terminal warning (see plan §8 risk 2). Textual itself degrades,
    # but a heads-up helps a first-time user.
    term = os.environ.get("TERM", "")
    if term in ("", "dumb", "screen") and not os.environ.get("TEXTUAL_NO_TERM_HINT"):
        print(
            f"[hint] TERM={term!r} may render poorly. Try "
            f"`TERM=xterm-256color meshtastic-mcp-test-tui ...` if the layout "
            f"looks broken.",
            file=sys.stderr,
        )

    app = _build_app(
        tx=tx,
        root=root,
        run_tests=run_tests,
        reportlog=reportlog,
        fwlog=fwlog,
        flashlog=flashlog,
        seed=seed,
        run_number=run_number,
        pytest_args=pytest_args,
    )

    # App.run() returns the subprocess exit code via `app.exit(returncode)`.
    return_value = app.run()
    if isinstance(return_value, int):
        return return_value
    return 0


# ---------------------------------------------------------------------------
# Everything below is only reachable once Textual is importable. `tx` is
# the namespace returned by `_import_textual()` so we don't scatter `from
# textual import ...` across the file.
# ---------------------------------------------------------------------------


def _build_app(
    *,
    tx: Any,
    root: pathlib.Path,
    run_tests: pathlib.Path,
    reportlog: pathlib.Path,
    fwlog: pathlib.Path,
    flashlog: pathlib.Path,
    seed: str,
    run_number: int,
    pytest_args: list[str],
) -> Any:
    """Assemble TestTuiApp with its Textual-dependent inner classes.

    Keeping the class definitions inside a factory means `main()` can
    short-circuit (--no-tui, terminal-check, argparse error) before we
    force Textual's import cost.
    """

    # Helper modules — lazy-imported here so the top-of-file import cost
    # only kicks in when main() has decided to run the TUI.
    from . import _flashlog as _flashlog_mod
    from . import _fwlog as _fwlog_mod
    from . import _history as _history_mod
    from . import _reproducer as _reproducer_mod
    from . import _uicap as _uicap_mod

    # ---------------- Messages ----------------

    class ReportLogEvent(tx.Message):
        def __init__(self, event: dict[str, Any]) -> None:
            self.event = event
            super().__init__()

    class PytestLine(tx.Message):
        def __init__(self, source: str, line: str) -> None:
            self.source = source  # "stdout" | "stderr"
            self.line = line
            super().__init__()

    class FirmwareLogLine(tx.Message):
        def __init__(self, record: dict[str, Any]) -> None:
            # {"ts": float, "port": str | None, "line": str}
            self.record = record
            super().__init__()

    class FlashLogLine(tx.Message):
        """Plain-text line from `tests/flash.log` — pio / esptool / nrfutil /
        picotool output tee'd by `pio._run_capturing`. Routed to the pytest
        pane so the operator sees live flash progress during `test_00_bake`
        instead of 3 minutes of pytest-captured silence."""

        def __init__(self, line: str) -> None:
            self.line = line
            super().__init__()

    class UiCaptureLine(tx.Message):
        """Live line from the UI-tier camera transcript — one per
        `frame_capture()` call. Posted only when the camera panel is
        enabled via `MESHTASTIC_UI_TUI_CAMERA=1`."""

        def __init__(self, test_id: str, line: str) -> None:
            self.test_id = test_id
            self.line = line
            super().__init__()

    class DeviceSnapshot(tx.Message):
        def __init__(self, rows: list[DeviceRow]) -> None:
            self.rows = rows
            super().__init__()

    class RunFinished(tx.Message):
        def __init__(self, returncode: int) -> None:
            self.returncode = returncode
            super().__init__()

    # ---------------- Workers ----------------

    class ReportlogWorker(threading.Thread):
        """Tail `reportlog.jsonl`, publish each event."""

        def __init__(self, app: Any, path: pathlib.Path, stop: threading.Event) -> None:
            super().__init__(daemon=True, name="reportlog-tail")
            self._app = app
            self._path = path
            self._stop = stop

        def run(self) -> None:
            # Wait up to 30 s for pytest to create the file (first call on
            # a cold cache can be slow).
            wait_deadline = time.monotonic() + 30.0
            while not self._path.is_file():
                if self._stop.is_set() or time.monotonic() > wait_deadline:
                    return
                time.sleep(0.1)
            try:
                fh = self._path.open("r", encoding="utf-8")
            except OSError:
                return
            try:
                while not self._stop.is_set():
                    line = fh.readline()
                    if not line:
                        time.sleep(0.05)
                        continue
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        event = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    self._app.post_message(ReportLogEvent(event))
            finally:
                fh.close()

    class SubprocessReaderWorker(threading.Thread):
        """Read one stream line-by-line and publish PytestLine messages."""

        def __init__(
            self,
            app: Any,
            stream: Any,
            source: str,
            stop: threading.Event,
        ) -> None:
            super().__init__(daemon=True, name=f"subprocess-{source}")
            self._app = app
            self._stream = stream
            self._source = source
            self._stop = stop

        def run(self) -> None:
            try:
                for line in iter(self._stream.readline, ""):
                    if self._stop.is_set():
                        break
                    self._app.post_message(
                        PytestLine(source=self._source, line=line.rstrip("\n"))
                    )
            except Exception:
                # stream closed / subprocess died; not fatal.
                pass

    class DevicePollerWorker(threading.Thread):
        """Poll list_devices() + device_info() at startup and after RunFinished.

        Deliberately NOT polling during the run — `hub_devices` is a
        session-scoped fixture holding SerialInterfaces across the whole
        session, and device_info() would deadlock on the exclusive port
        lock. Header shows "(stale)" during the gap.
        """

        def __init__(self, app: Any, state: State, stop: threading.Event) -> None:
            super().__init__(daemon=True, name="device-poller")
            self._app = app
            self._state = state
            self._stop = stop
            self._trigger = threading.Event()

        def trigger(self) -> None:
            self._trigger.set()

        def run(self) -> None:
            # Perform one poll at startup; then wait for explicit triggers.
            self._poll_once()
            while not self._stop.is_set():
                if self._trigger.wait(timeout=0.5):
                    self._trigger.clear()
                    if self._stop.is_set():
                        break
                    with self._state.lock:
                        active = self._state.run_active
                    if active:
                        continue
                    self._poll_once()

        def _poll_once(self) -> None:
            try:
                from meshtastic_mcp import devices as devices_mod
                from meshtastic_mcp import info as info_mod
            except Exception as exc:  # pragma: no cover
                self._app.post_message(
                    PytestLine(
                        source="stderr", line=f"[tui] device import failed: {exc!r}"
                    )
                )
                return
            rows: list[DeviceRow] = []
            try:
                raw = devices_mod.list_devices(include_unknown=True)
            except Exception as exc:
                self._app.post_message(
                    PytestLine(
                        source="stderr", line=f"[tui] list_devices failed: {exc!r}"
                    )
                )
                return
            for d in raw:
                vid_raw = d.get("vid") or ""
                try:
                    vid_i = (
                        int(vid_raw, 16)
                        if isinstance(vid_raw, str) and vid_raw.startswith("0x")
                        else int(vid_raw)
                    )
                except (TypeError, ValueError):
                    vid_i = 0
                role = None
                if vid_i == 0x239A:
                    role = "nrf52"
                elif vid_i in (0x303A, 0x10C4):
                    role = "esp32s3"
                if not role and not d.get("likely_meshtastic"):
                    continue
                row = DeviceRow(
                    role=role,
                    port=d.get("port", ""),
                    vid=str(vid_raw),
                    pid=str(d.get("pid") or ""),
                    description=d.get("description", "") or "",
                )
                if role:
                    try:
                        row.info = info_mod.device_info(port=row.port, timeout_s=6.0)
                    except Exception as exc:
                        row.info = {"error": repr(exc)}
                rows.append(row)
            self._app.post_message(DeviceSnapshot(rows=rows))

    # ---------------- Modals ----------------

    class FailureDetailScreen(tx.ModalScreen):
        """Show a failed test's longrepr + captured sections."""

        BINDINGS = [tx.Binding("escape,q", "dismiss", "close")]

        def __init__(self, leaf: LeafReport, report_html: pathlib.Path) -> None:
            self._leaf = leaf
            self._report_html = report_html
            super().__init__()

        def compose(self) -> Any:
            yield tx.Static(
                f"[bold]{self._leaf.nodeid}[/bold]   "
                f"outcome=[red]{self._leaf.outcome}[/red]   "
                f"duration={_format_duration(self._leaf.duration_s)}",
                id="failure-detail-header",
            )
            log = tx.RichLog(
                highlight=False, markup=False, wrap=False, id="failure-detail-log"
            )
            yield log
            yield tx.Static(
                f"[dim]Full HTML report: {self._report_html}[/dim]   [esc] close",
                id="failure-detail-footer",
            )

        def on_mount(self) -> None:
            log = self.query_one("#failure-detail-log", tx.RichLog)
            if self._leaf.longrepr:
                log.write(self._leaf.longrepr)
                log.write("")
            for section_name, section_text in self._leaf.sections:
                log.write(f"--- {section_name} ---")
                log.write(section_text)
                log.write("")
            if not self._leaf.longrepr and not self._leaf.sections:
                log.write("(no longrepr or captured sections in reportlog event)")

        def action_dismiss(self, _result: Any = None) -> None:
            self.dismiss()

    class FilterInputScreen(tx.ModalScreen[str]):
        """Prompt the user for a tree filter substring (empty clears)."""

        BINDINGS = [tx.Binding("escape", "cancel", "cancel")]

        def compose(self) -> Any:
            yield tx.Static("filter test tree (substring, empty = clear):")
            yield tx.Input(placeholder="nodeid substring", id="filter-input")

        def on_input_submitted(self, event: Any) -> None:
            self.dismiss(event.value.strip())

        def action_cancel(self) -> None:
            self.dismiss(None)

    class CoverageModal(tx.ModalScreen):
        """Read `tests/tool_coverage.json` (written by `tests/tool_coverage.py`
        at `pytest_sessionfinish`) and render a two-column summary of which
        MCP tools got exercised by the run. `(no coverage data yet)` while
        the run is in flight."""

        BINDINGS = [tx.Binding("escape,q,c", "dismiss", "close")]

        def __init__(self, coverage_path: pathlib.Path) -> None:
            self._path = coverage_path
            super().__init__()

        def compose(self) -> Any:
            yield tx.Static("[bold]MCP tool coverage[/bold]", id="coverage-header")
            yield tx.RichLog(
                highlight=False, markup=True, wrap=False, id="coverage-log"
            )
            yield tx.Static(
                f"[dim]{self._path}[/dim]   [esc] close",
                id="coverage-footer",
            )

        def on_mount(self) -> None:
            log = self.query_one("#coverage-log", tx.RichLog)
            if not self._path.is_file():
                log.write("(no coverage data — tool_coverage.json not written yet)")
                log.write("")
                log.write("Coverage is emitted at pytest_sessionfinish; this")
                log.write("file appears after the suite completes.")
                return
            try:
                data = json.loads(self._path.read_text(encoding="utf-8"))
            except Exception as exc:
                log.write(f"[red]failed to read {self._path}:[/red] {exc!r}")
                return
            calls = data.get("calls") or {}
            if not calls:
                log.write("(tool_coverage.json present but no calls recorded)")
                return
            exercised = sorted(
                ((n, c) for n, c in calls.items() if c > 0), key=lambda x: -x[1]
            )
            unexercised = sorted(n for n, c in calls.items() if c == 0)
            log.write(f"[b]{len(exercised)} / {len(calls)} MCP tools exercised[/b]")
            log.write("")
            log.write("[green]exercised[/green] (count):")
            for name, count in exercised:
                log.write(f"  {count:>4}  {name}")
            if unexercised:
                log.write("")
                log.write("[dim]not exercised:[/dim]")
                for name in unexercised:
                    log.write(f"        {name}")

        def action_dismiss(self, _result: Any = None) -> None:
            self.dismiss()

    class ReproducerResultModal(tx.ModalScreen):
        """Show the exported reproducer tarball path with a short instruction."""

        BINDINGS = [tx.Binding("escape,q,enter", "dismiss", "close")]

        def __init__(
            self, archive_path: pathlib.Path, error: str | None = None
        ) -> None:
            self._archive = archive_path
            self._error = error
            super().__init__()

        def compose(self) -> Any:
            if self._error:
                yield tx.Static(f"[red]Reproducer export failed:[/red] {self._error}")
            else:
                yield tx.Static("[bold green]Reproducer bundle written[/bold green]")
                yield tx.Static(f"[cyan]{self._archive}[/cyan]")
                yield tx.Static("")
                yield tx.Static(
                    "Contains: README.md, test_report.json, fwlog.jsonl (time-filtered),"
                )
                yield tx.Static(
                    "devices.json, env.json. Attach to an issue / paste the path in chat."
                )
            yield tx.Static("")
            yield tx.Static("[dim][esc] close[/dim]")

        def action_dismiss(self, _result: Any = None) -> None:
            self.dismiss()

    # ---------------- App ----------------

    class TestTuiApp(tx.App):
        CSS = """
        Screen { layout: vertical; }
        #header-bar { height: 2; padding: 0 1; background: $panel; }
        #tier-table { height: auto; max-height: 11; }
        #body { height: 1fr; }
        #tree-pane { width: 50%; border-right: solid $primary-background; }
        #right-pane { width: 50%; layout: vertical; }
        #pytest-pane { height: 50%; border-bottom: solid $primary-background; }
        #fwlog-header { height: 1; padding: 0 1; background: $panel; }
        #fwlog-pane { height: 1fr; }
        #uicap-header { height: 1; padding: 0 1; background: $boost; }
        #uicap-pane { height: 14; border-top: solid $primary-background; }
        #uicap-image { width: 36; border-right: solid $primary-background; padding: 0 1; }
        #uicap-log { width: 1fr; height: 14; }
        Tree { height: 100%; }
        RichLog { height: 100%; }
        #device-table { height: auto; max-height: 6; }
        """

        TITLE = "mcp-server test runner"

        BINDINGS = [
            tx.Binding("r", "rerun_focused", "re-run focused"),
            tx.Binding("f", "filter_tree", "filter"),
            tx.Binding("d", "failure_detail", "failure detail"),
            tx.Binding("g", "open_html_report", "open report.html"),
            tx.Binding("x", "export_reproducer", "export reproducer"),
            tx.Binding("c", "coverage_panel", "coverage"),
            tx.Binding("l", "cycle_fwlog_filter", "fw log filter"),
            tx.Binding("q,ctrl+c", "quit_app", "quit"),
        ]

        def __init__(self) -> None:
            super().__init__()
            self._state = State()
            self._root = root
            self._run_tests = run_tests
            self._reportlog = reportlog
            self._fwlog = fwlog
            self._flashlog = flashlog
            self._report_html = root / _REPORT_HTML_RELATIVE
            self._tool_coverage = root / _TOOL_COVERAGE_RELATIVE
            self._repro_dir = root / _REPRODUCERS_RELATIVE
            self._seed = seed
            self._run_number = run_number
            self._pytest_args = pytest_args
            self._start_time = time.monotonic()
            self._proc: subprocess.Popen[str] | None = None
            self._stop = threading.Event()
            self._reportlog_worker: ReportlogWorker | None = None
            self._stdout_worker: SubprocessReaderWorker | None = None
            self._stderr_worker: SubprocessReaderWorker | None = None
            self._device_worker: DevicePollerWorker | None = None
            self._fwlog_worker: _fwlog_mod.FirmwareLogTailer | None = None
            self._flashlog_worker: _flashlog_mod.FlashLogTailer | None = None
            self._uicap_worker: _uicap_mod.UiCaptureTailer | None = None
            # Env-gated; only mounts the UI-capture panel when operator asks for it.
            self._ui_camera_enabled = bool(
                int(os.environ.get("MESHTASTIC_UI_TUI_CAMERA", "0") or "0")
            )
            self._tree_filter: str = ""
            self._sigint_count = 0
            # Firmware-log port filter: None = all, else exact port match.
            self._fwlog_filter: str | None = None
            # Ordered set of distinct ports we've seen firmware log lines
            # from — the `l` key cycles through these.
            self._fwlog_ports: list[str] = []
            # Cross-run history.
            self._history_store = _history_mod.HistoryStore(
                root / _HISTORY_RELATIVE, keep_last=40
            )
            self._history_cache = self._history_store.read_recent()

        # -------- composition / mount --------

        def compose(self) -> Any:
            yield tx.Static(self._header_text(), id="header-bar")
            tier_table = tx.DataTable(id="tier-table", show_cursor=False)
            yield tier_table
            with tx.Horizontal(id="body"):
                with tx.Vertical(id="tree-pane"):
                    yield tx.Tree("tests", id="test-tree")
                with tx.Vertical(id="right-pane"):
                    with tx.Vertical(id="pytest-pane"):
                        yield tx.RichLog(
                            id="pytest-log",
                            highlight=False,
                            markup=False,
                            wrap=False,
                            max_lines=5000,
                        )
                    yield tx.Static(self._fwlog_header_text(), id="fwlog-header")
                    with tx.Vertical(id="fwlog-pane"):
                        yield tx.RichLog(
                            id="fwlog-log",
                            highlight=False,
                            markup=False,
                            # `wrap=True` so long firmware log lines (some
                            # hit ~200 chars — full packet hex dumps plus
                            # source tags) don't get truncated at the
                            # right edge. The right pane is ~50% of the
                            # terminal so even a wide terminal has a
                            # ~90-char cap; plain truncation dropped the
                            # uptime counter or packet id off the end.
                            wrap=True,
                            max_lines=5000,
                        )
                    if self._ui_camera_enabled:
                        yield tx.Static(
                            "UI camera — latest capture + transcript   (MESHTASTIC_UI_TUI_CAMERA=1)",
                            id="uicap-header",
                        )
                        with tx.Horizontal(id="uicap-pane"):
                            yield tx.Static(
                                "(waiting…)", id="uicap-image", markup=False
                            )
                            yield tx.RichLog(
                                id="uicap-log",
                                highlight=False,
                                markup=False,
                                wrap=True,
                                max_lines=500,
                            )
            yield tx.DataTable(id="device-table", show_cursor=False)
            yield tx.Footer()

        def _fwlog_header_text(self) -> str:
            filt = self._fwlog_filter or "(all ports)"
            return f"firmware log   filter: [b]{filt}[/b]   [l] cycle"

        def on_mount(self) -> None:
            # Tier-counters table. `add_column` (singular) lets us pick
            # the key explicitly — `add_columns` (plural) in textual 8.x
            # returns auto-generated keys that are tedious to track
            # separately, and update_cell(column_key=<label>) silently
            # no-ops because the key is not the label. "Progress" is the
            # new v2 column — a small [=====  ] bar; see `_progress_bar`.
            tier_table = self.query_one("#tier-table", tx.DataTable)
            for col in (
                "Tier",
                "Passed",
                "Failed",
                "Skipped",
                "Running",
                "Remaining",
                "Progress",
            ):
                tier_table.add_column(col, key=col)
            for t in TIERS:
                tier_table.add_row(t, "0", "0", "0", "0", "0", "", key=t)
            # Device table. "Status" shows which test (if any) is currently
            # running on this device — derived from the running_nodeid plus
            # role inference from the nodeid's `[...]` parametrization.
            dev_table = self.query_one("#device-table", tx.DataTable)
            for col in (
                "Role",
                "Port",
                "Firmware",
                "HW",
                "Region",
                "Channel",
                "Peers",
                "Status",
            ):
                dev_table.add_column(col, key=col)
            # Launch workers + subprocess
            self._device_worker = DevicePollerWorker(self, self._state, self._stop)
            self._device_worker.start()
            self._reportlog_worker = ReportlogWorker(self, self._reportlog, self._stop)
            self._reportlog_worker.start()
            # Firmware log tail worker — publishes FirmwareLogLine messages.
            self._fwlog_worker = _fwlog_mod.FirmwareLogTailer(
                path=self._fwlog,
                post=lambda rec: self.post_message(FirmwareLogLine(rec)),
                stop=self._stop,
            )
            self._fwlog_worker.start()
            # Flash log tail worker — plain-text pio/esptool/nrfutil/picotool
            # output tee'd by `pio._run_capturing`. Routes each line into the
            # pytest pane so the operator has live feedback during long flash
            # operations (`pio run -t upload` is ~3 min of silence otherwise).
            self._flashlog_worker = _flashlog_mod.FlashLogTailer(
                path=self._flashlog,
                post=lambda line: self.post_message(FlashLogLine(line)),
                stop=self._stop,
            )
            self._flashlog_worker.start()
            # UI-capture transcript tailer — only runs when the camera panel
            # is enabled. Watches tests/ui_captures/**/transcript.md for new
            # lines as UI tests execute.
            if self._ui_camera_enabled:
                captures_root = self._root / "mcp-server" / "tests" / "ui_captures"
                # When the TUI is launched from inside mcp-server (the usual
                # case), `self._root` is already mcp-server/, so adjust:
                if not captures_root.parent.name == "mcp-server":
                    captures_root = self._root / "tests" / "ui_captures"
                self._uicap_worker = _uicap_mod.UiCaptureTailer(
                    root=captures_root,
                    post=lambda tid, line: self.post_message(UiCaptureLine(tid, line)),
                    stop=self._stop,
                )
                self._uicap_worker.start()
            self._spawn_pytest(self._pytest_args)
            # Header tick (seed / runtime / sparkline re-renders at 1 Hz).
            # Also refreshes the device-status column so the per-test elapsed
            # time climbs live during silent test bodies (flash, long mesh
            # timeouts, etc.) — cheap: device-table is 1-2 rows.
            self.set_interval(1.0, self._on_tick)

        def _header_text(self) -> str:
            elapsed = time.monotonic() - self._start_time
            phase = (
                "running"
                if self._state.run_active
                else ("done" if self._state.exit_code is not None else "starting")
            )
            stale = " (devices stale)" if self._state.run_active else ""
            # Sparkline over recent run durations (oldest → newest).
            spark = _history_mod.sparkline(
                (r.duration_s for r in self._history_cache), width=20
            )
            spark_segment = f"   history: {spark}" if spark else ""
            return (
                f"mcp-server test runner   "
                f"seed: [b]{self._seed}[/b]   "
                f"run #{self._run_number}   "
                f"elapsed {_format_duration(elapsed)}   "
                f"phase: [b]{phase}[/b]{stale}"
                f"{spark_segment}"
            )

        def _refresh_header(self) -> None:
            try:
                self.query_one("#header-bar", tx.Static).update(self._header_text())
            except Exception:
                pass

        def _on_tick(self) -> None:
            """1 Hz tick: refresh header clock + any live-updating cells.

            The device-status cell embeds the running test's elapsed time
            (`RUNNING: test_bake_nrf52 (1:23)`), which needs to re-render
            each second during long silent test bodies. Cheap — O(devices),
            which is 1–2 rows in practice. Skipped when no test is
            running so we don't burn cycles when the TUI is idle.
            """
            self._refresh_header()
            if self._state.running_started_at is not None:
                self._refresh_device_status()

        # -------- subprocess management --------

        def _spawn_pytest(self, extra_args: list[str]) -> None:
            env = os.environ.copy()
            env.setdefault("MESHTASTIC_MCP_SEED", self._seed)
            cmd = [str(self._run_tests), *extra_args]
            # `run-tests.sh` has a `[ "$#" -eq 0 ]` guard that applies the
            # full default-args set:
            #     tests/ --html=tests/report.html --self-contained-html
            #     --junitxml=tests/junit.xml -v --tb=short
            # plus an unconditional `--report-log` append at the end. If we
            # pre-append `--report-log` here when `extra_args` is empty, $#
            # becomes 1 and the whole defaults block is skipped — pytest
            # then runs without the `tests/` positional (discovers from the
            # mcp-server root and potentially drags in production modules
            # named `test_*.py`), without the HTML/junit reports the /test
            # skill relies on for failure interpretation, and without
            # `-v --tb=short` output formatting.
            #
            # So: only append `--report-log` when the operator explicitly
            # passed pytest args (e.g. the `r`-key re-run-focused-test
            # case, where the wrapper's defaults are already bypassed by
            # the explicit arg). Trust the wrapper's own injection in the
            # no-args path.
            if extra_args and not any(a.startswith("--report-log") for a in cmd):
                cmd.append(f"--report-log={self._reportlog}")
            log = self.query_one("#pytest-log", tx.RichLog)
            log.write(f"$ {' '.join(cmd)}")
            try:
                self._proc = subprocess.Popen(  # noqa: S603
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    bufsize=1,
                    text=True,
                    start_new_session=True,
                    env=env,
                    cwd=str(self._root),
                )
            except Exception as exc:
                log.write(f"[tui] failed to spawn pytest: {exc!r}")
                return
            with self._state.lock:
                self._state.run_active = True
                self._state.exit_code = None
            self._stdout_worker = SubprocessReaderWorker(
                self, self._proc.stdout, "stdout", self._stop
            )
            self._stdout_worker.start()
            self._stderr_worker = SubprocessReaderWorker(
                self, self._proc.stderr, "stderr", self._stop
            )
            self._stderr_worker.start()
            # Watchdog thread that posts RunFinished when the subprocess exits.
            threading.Thread(
                target=self._watch_exit, daemon=True, name="pytest-watch"
            ).start()

        def _watch_exit(self) -> None:
            if self._proc is None:
                return
            rc = self._proc.wait()
            self.post_message(RunFinished(returncode=rc))

        # -------- message handlers --------

        def on_report_log_event(self, message: Any) -> None:
            ev = message.event
            rt = ev.get("$report_type")
            if rt == "SessionStart":
                return
            if rt == "CollectReport":
                # pytest-reportlog emits CollectReport once per collected
                # node (directory, module, class, session). Leaf items
                # appear as nodeids in the `result` array; parent-level
                # collections have empty `result`.
                for item in ev.get("result") or []:
                    self._register_leaf(item.get("nodeid", ""))
                return
            if rt == "TestReport":
                nodeid = ev.get("nodeid", "")
                when = ev.get("when")
                outcome = ev.get("outcome")

                # Phase 1: update the "currently running" marker that
                # drives the device-status strip. `when="setup"` +
                # outcome=passed means the test body is about to execute;
                # `when="call"` (any outcome) means it just finished;
                # `when="setup"` + outcome in {failed, skipped} also
                # clears, since the body will never run.
                if when == "setup" and outcome == "passed":
                    self._state.running_nodeid = nodeid
                    self._state.running_started_at = time.monotonic()
                    self._refresh_device_status()
                elif when == "call" or (
                    when == "setup" and outcome in ("failed", "skipped")
                ):
                    if self._state.running_nodeid == nodeid:
                        self._state.running_nodeid = None
                        self._state.running_started_at = None
                        self._refresh_device_status()

                # Phase 2: emit an authoritative leaf outcome.
                #   `call` + terminal: test body ran.
                #   `setup` + failed:  promote setup error to a failed leaf.
                #   `setup` + skipped: fixture-level `pytest.skip(...)`.
                #                      Our `mesh_pair`, `baked_mesh`, and
                #                      `hub_devices` fixtures all do this
                #                      when a role isn't detected or the
                #                      bake doesn't match. Without this
                #                      branch, those tests would never
                #                      register in the tree and the tier
                #                      counters would silently lie — e.g.
                #                      the telemetry tier showed 0/0/0
                #                      while 4 tests were actually skipped.
                #   `rerun` (pytest-rerunfailures): rewind to pending.
                # Teardown outcomes are intentionally ignored — a
                # teardown failure shouldn't overwrite the call's
                # authoritative pass/fail.
                if when == "call" and outcome in ("passed", "failed", "skipped"):
                    self._apply_outcome(nodeid, outcome, ev)
                elif when == "setup" and outcome in ("failed", "skipped"):
                    self._apply_outcome(nodeid, outcome, ev)
                elif outcome == "rerun":
                    self._apply_outcome(nodeid, "pending", ev)
                return
            if rt == "SessionFinish":
                return
            # Unknown — ignore silently.

        def on_pytest_line(self, message: Any) -> None:
            log = self.query_one("#pytest-log", tx.RichLog)
            prefix = "" if message.source == "stdout" else "[stderr] "
            log.write(f"{prefix}{message.line}")

        def on_flash_log_line(self, message: Any) -> None:
            """Route a pio/esptool/nrfutil line into the pytest pane.

            Prefixed `[flash]` so the operator can visually separate
            tee'd subprocess output from pytest's own stdout. Without this
            routing, long flash operations are a 3-minute black hole of
            pytest-captured silence.
            """
            log = self.query_one("#pytest-log", tx.RichLog)
            log.write(f"[flash] {message.line}")

        def on_ui_capture_line(self, message: Any) -> None:
            """Route a UI-capture transcript line into the camera panel.

            Each line is already formatted by frame_capture — e.g.
            `1. **initial** — frame 2/8 name=home — OCR: ...`. We write
            the text into the RichLog AND try to render the corresponding
            PNG on the left side (requires rich-pixels, Pillow).
            """
            if not self._ui_camera_enabled:
                return
            try:
                log_panel = self.query_one("#uicap-log", tx.RichLog)
            except Exception:
                return
            log_panel.write(f"[{message.test_id}] {message.line}")
            self._render_latest_ui_capture(message.test_id, message.line)

        def _render_latest_ui_capture(self, test_id: str, line: str) -> None:
            """Find the PNG that corresponds to `line` and render it on the
            left of the uicap pane. Soft-fails if rich-pixels isn't
            installed or the PNG isn't found — operator still has the text
            transcript on the right.
            """
            try:
                from PIL import Image  # type: ignore[import-untyped]
                from rich_pixels import Pixels  # type: ignore[import-untyped]
            except ImportError:
                return

            # Transcript lines look like `1. **label** — ...`. Pull the leading
            # integer to locate the capture file.
            import re as _re

            m = _re.match(r"\s*(\d+)\.\s", line)
            if not m:
                return
            step = int(m.group(1))

            # Captures directory is sibling of tests/ — mirror the path the
            # tailer watches. Search both likely layouts (in-mcp-server vs.
            # firmware-root invocation).
            candidates = [
                self._root / "tests" / "ui_captures",
                self._root / "mcp-server" / "tests" / "ui_captures",
            ]
            captures_root = next((p for p in candidates if p.is_dir()), None)
            if captures_root is None:
                return

            # Drill into <session_seed>/<test_id>/ — test_id is the
            # sanitized nodeid the tailer already passed through.
            matches = list(captures_root.rglob(f"{test_id}/{step:03d}-*.png"))
            if not matches:
                return
            png_path = matches[-1]

            try:
                img = Image.open(png_path).convert("RGB")
                # Resize to fit ~32 cells wide × ~12 rows tall (half-block
                # renderer gives 2× vertical resolution, so 32×24 px input
                # lands at ~32×12 cells). Keep aspect ratio.
                target_w = 60
                w, h = img.size
                target_h = max(1, int(h * (target_w / max(1, w))))
                # Clamp: the image panel is 14 rows; half-blocks give 2 rows
                # per vertical cell, so cap pixel height at ~26.
                target_h = min(target_h, 26)
                img = img.resize((target_w, target_h))
                pixels = Pixels.from_image(img)
            except Exception:
                return

            try:
                image_widget = self.query_one("#uicap-image", tx.Static)
                image_widget.update(pixels)
            except Exception:
                pass

        def on_firmware_log_line(self, message: Any) -> None:
            rec = message.record
            port = rec.get("port")
            line = rec.get("line", "")
            # Track distinct ports for `l` filter cycling. The ordered-set
            # trick — list membership — is fine here because `_fwlog_ports`
            # is tiny (2-3 entries for a typical lab).
            if port and port not in self._fwlog_ports:
                self._fwlog_ports.append(port)
                # Refresh the fwlog header to show the newly-available port.
                try:
                    self.query_one("#fwlog-header", tx.Static).update(
                        self._fwlog_header_text()
                    )
                except Exception:
                    pass
            # Filter: None = show all; otherwise exact port match.
            if self._fwlog_filter and port != self._fwlog_filter:
                return
            log = self.query_one("#fwlog-log", tx.RichLog)
            port_tag = ""
            if port:
                # Show only the last path component — `/dev/cu.usbmodem1101`
                # is long; `usbmodem1101` is enough when the filter is
                # "all".
                tail = port.rsplit("/", 1)[-1]
                port_tag = f"[{tail}] "
            log.write(f"{port_tag}{line}")

        @staticmethod
        def _progress_bar(counters: TierCounters, *, width: int = 10) -> str:
            done = counters.passed + counters.failed + counters.skipped
            total = done + counters.running + counters.remaining
            if total <= 0:
                return ""
            filled = int(round(width * done / total))
            bar = "█" * filled + "·" * (width - filled)
            return f"{bar} {done}/{total}"

        def on_device_snapshot(self, message: Any) -> None:
            with self._state.lock:
                self._state.devices = list(message.rows)
            dev_table = self.query_one("#device-table", tx.DataTable)
            dev_table.clear()
            for row in message.rows:
                info = row.info or {}
                role = row.role or "?"
                fw = info.get("firmware_version", "—")
                hw = info.get("hw_model", "—")
                region = info.get("region", "—")
                channel = info.get("primary_channel", "—")
                peers = info.get("num_nodes")
                if peers is None:
                    peers = "—"
                else:
                    peers = str(max(int(peers) - 1, 0))  # exclude self
                status = self._status_for_role(role)
                # Row key = port path (stable, unique, survives re-snapshots).
                dev_table.add_row(
                    role,
                    row.port,
                    str(fw),
                    str(hw),
                    str(region),
                    str(channel),
                    peers,
                    status,
                    key=row.port,
                )

        def _status_for_role(self, role: str) -> str:
            """Status cell for the given role: 'idle' or 'RUNNING: <short> (M:SS)'.

            A running test whose nodeid doesn't carry an explicit role
            parametrization (no `[...]` bracket) is treated as touching
            every device — that matches how `test_bidirectional` and the
            pytest_sessionstart-level tests work in practice.

            The trailing `(M:SS)` is live-updated by `_on_tick` at 1 Hz
            and gives the operator concrete "still running" evidence for
            long-silent test bodies (flash, long mesh timeouts).
            """
            nodeid = self._state.running_nodeid
            if not nodeid:
                return "idle"
            roles = _roles_from_nodeid(nodeid)
            if roles and role not in roles:
                return "idle"
            short = _testname_of_nodeid(nodeid)
            # Compute elapsed for the live counter. Budget 8 chars at the
            # end of the cell — `(12:34)` plus a space. Shorten `short`
            # first, then tack on the elapsed suffix.
            started = self._state.running_started_at
            elapsed_suffix = ""
            if started is not None:
                elapsed_suffix = f" ({_format_duration(time.monotonic() - started)})"
            # Truncate the test name to fit; Status column is the
            # rightmost column and the device table is horizontally
            # short. 40 chars + the elapsed suffix keeps the
            # parametrization suffix visible for mesh_pair tests.
            if len(short) > 40:
                short = short[:37] + "…"
            return f"RUNNING: {short}{elapsed_suffix}"

        def _refresh_device_status(self) -> None:
            """Update the Status cell for every detected device.

            Called whenever `running_nodeid` transitions (setup → call).
            Cheap: O(devices) which is 1–2 rows in practice.
            """
            try:
                dev_table = self.query_one("#device-table", tx.DataTable)
            except Exception:
                return
            for row in self._state.devices:
                role = row.role or "?"
                try:
                    dev_table.update_cell(
                        row.port, "Status", self._status_for_role(role)
                    )
                except Exception:
                    # Row key might not exist yet if a snapshot hasn't
                    # populated it — harmless; next snapshot will carry
                    # the fresh status value.
                    pass

        def on_run_finished(self, message: Any) -> None:
            with self._state.lock:
                self._state.run_active = False
                self._state.exit_code = message.returncode
            log = self.query_one("#pytest-log", tx.RichLog)
            log.write(f"[tui] pytest exited with {message.returncode}")
            # Trigger a fresh device poll now that ports are free again.
            if self._device_worker is not None:
                self._device_worker.trigger()
            # Persist a history record — one line per run, tailed by the
            # sparkline on every subsequent TUI launch.
            duration_s = time.monotonic() - self._start_time
            passed = sum(t.passed for t in self._state.tiers.values())
            failed = sum(t.failed for t in self._state.tiers.values())
            skipped = sum(t.skipped for t in self._state.tiers.values())
            try:
                rec = self._history_store.record_run(
                    run=self._run_number,
                    duration_s=duration_s,
                    passed=passed,
                    failed=failed,
                    skipped=skipped,
                    exit_code=message.returncode,
                    seed=self._seed,
                )
                self._history_cache = self._history_store.read_recent()
                log.write(
                    f"[tui] history: recorded run #{rec.run} "
                    f"({passed}p/{failed}f/{skipped}s in {_format_duration(duration_s)})"
                )
            except Exception as exc:
                log.write(f"[tui] history persist failed: {exc!r}")

        # -------- tree + counters --------

        def _register_leaf(self, nodeid: str) -> None:
            if not nodeid or nodeid in self._state.leaves:
                return
            tier = _tier_of_nodeid(nodeid)
            leaf = LeafReport(nodeid=nodeid, tier=tier)
            self._state.leaves[nodeid] = leaf
            self._state.nodeid_order.append(nodeid)
            counters = self._state.tiers.get(tier)
            if counters is not None:
                counters.remaining += 1
                self._refresh_tier_row(tier)
            self._add_to_tree(leaf)

        def _apply_outcome(self, nodeid: str, outcome: str, ev: dict[str, Any]) -> None:
            if not nodeid:
                return
            leaf = self._state.leaves.get(nodeid)
            if leaf is None:
                # First event for this nodeid is the report itself (no
                # collection event seen) — register on the fly.
                self._register_leaf(nodeid)
                leaf = self._state.leaves[nodeid]
            prev = leaf.outcome
            leaf.outcome = outcome
            leaf.duration_s = float(ev.get("duration", 0.0) or 0.0)
            # Wall-clock start/stop — pytest-reportlog emits these as
            # float seconds (Unix epoch). Used by the reproducer exporter
            # to window fwlog.jsonl down to just the failure's context.
            start = ev.get("start")
            stop = ev.get("stop")
            if isinstance(start, (int, float)):
                leaf.start_ts = float(start)
            if isinstance(stop, (int, float)):
                leaf.stop_ts = float(stop)
            longrepr = ev.get("longrepr") or ""
            if isinstance(longrepr, dict):
                # pytest-reportlog may serialize as {"reprcrash": ..., "reprtraceback": ...}
                longrepr = json.dumps(longrepr, indent=2, default=str)
            leaf.longrepr = longrepr
            sections = ev.get("sections") or []
            if isinstance(sections, list):
                leaf.sections = [
                    (
                        (s[0], s[1])
                        if isinstance(s, (list, tuple)) and len(s) >= 2
                        else ("section", str(s))
                    )
                    for s in sections
                ]
            counters = self._state.tiers.get(leaf.tier)
            if counters is None:
                return
            # Undo prior bucket, apply new one.
            if prev in ("passed", "failed", "skipped"):
                setattr(counters, prev, max(getattr(counters, prev) - 1, 0))
            elif prev == "pending":
                counters.remaining = max(counters.remaining - 1, 0)
            if outcome in ("passed", "failed", "skipped"):
                setattr(counters, outcome, getattr(counters, outcome) + 1)
            elif outcome == "pending":
                counters.remaining += 1
            self._refresh_tier_row(leaf.tier)
            self._refresh_tree_leaf(leaf)

        def _refresh_tier_row(self, tier: str) -> None:
            counters = self._state.tiers.get(tier)
            if counters is None:
                return
            try:
                table = self.query_one("#tier-table", tx.DataTable)
                table.update_cell(tier, column_key="Passed", value=str(counters.passed))
                table.update_cell(tier, column_key="Failed", value=str(counters.failed))
                table.update_cell(
                    tier, column_key="Skipped", value=str(counters.skipped)
                )
                table.update_cell(
                    tier, column_key="Running", value=str(counters.running)
                )
                table.update_cell(
                    tier, column_key="Remaining", value=str(counters.remaining)
                )
                table.update_cell(
                    tier, column_key="Progress", value=self._progress_bar(counters)
                )
            except Exception:
                # Column-key API differs slightly across textual versions;
                # fall back to positional update if needed.
                pass

        def _add_to_tree(self, leaf: LeafReport) -> None:
            tree = self.query_one("#test-tree", tx.Tree)
            root_node = tree.root
            tier_node = None
            for child in root_node.children:
                if str(child.label).split(" ")[0] == leaf.tier:
                    tier_node = child
                    break
            if tier_node is None:
                tier_node = root_node.add(leaf.tier, expand=False)
            file_name = _file_of_nodeid(leaf.nodeid)
            file_node = None
            for child in tier_node.children:
                if str(child.label).split(" ")[0] == file_name:
                    file_node = child
                    break
            if file_node is None:
                file_node = tier_node.add(file_name, expand=False)
            glyph = self._glyph_for(leaf.outcome)
            file_node.add_leaf(
                f"{glyph} {_testname_of_nodeid(leaf.nodeid)}", data=leaf.nodeid
            )

        def _refresh_tree_leaf(self, leaf: LeafReport) -> None:
            tree = self.query_one("#test-tree", tx.Tree)
            glyph = self._glyph_for(leaf.outcome)
            label = f"{glyph} {_testname_of_nodeid(leaf.nodeid)}"
            for tier_node in tree.root.children:
                if str(tier_node.label).split(" ")[0] != leaf.tier:
                    continue
                for file_node in tier_node.children:
                    if str(file_node.label).split(" ")[0] != _file_of_nodeid(
                        leaf.nodeid
                    ):
                        continue
                    for leaf_node in file_node.children:
                        if getattr(leaf_node, "data", None) == leaf.nodeid:
                            leaf_node.set_label(label)
                            return

        @staticmethod
        def _glyph_for(outcome: str) -> str:
            return {
                "passed": "[green]✓[/green]",
                "failed": "[red]✗[/red]",
                "skipped": "[dim]○[/dim]",
                "pending": "·",
                "running": "[yellow]●[/yellow]",
            }.get(outcome, "?")

        # -------- actions --------

        def action_rerun_focused(self) -> None:
            if self._state.run_active:
                self.bell()
                return
            tree = self.query_one("#test-tree", tx.Tree)
            node = tree.cursor_node
            if node is None:
                self.bell()
                return
            target: str | None = None
            if getattr(node, "data", None):
                target = str(node.data)  # leaf: full nodeid
            else:
                # Internal node — derive a pytest arg.
                labels = []
                cur: Any = node
                while cur is not None and cur.parent is not None:
                    labels.append(str(cur.label).split(" ")[0])
                    cur = cur.parent
                if labels:
                    target = "tests/" + "/".join(reversed(labels))
            if not target:
                self.bell()
                return
            # Reset state + tree for the new run.
            self._reset_for_rerun()
            self._spawn_pytest([target])

        def action_filter_tree(self) -> None:
            def _apply(value: str | None) -> None:
                if value is None:
                    return
                self._tree_filter = value
                self._apply_tree_filter()

            self.push_screen(FilterInputScreen(), _apply)

        def _apply_tree_filter(self) -> None:
            tree = self.query_one("#test-tree", tx.Tree)
            needle = self._tree_filter.lower()
            for tier_node in tree.root.children:
                tier_match_count = 0
                for file_node in tier_node.children:
                    file_match_count = 0
                    for leaf_node in file_node.children:
                        nodeid = str(getattr(leaf_node, "data", "") or "")
                        match = (not needle) or (needle in nodeid.lower())
                        leaf_node.display = match
                        if match:
                            file_match_count += 1
                    file_node.display = file_match_count > 0 or not needle
                    if file_match_count > 0:
                        tier_match_count += 1
                tier_node.display = tier_match_count > 0 or not needle

        def action_failure_detail(self) -> None:
            tree = self.query_one("#test-tree", tx.Tree)
            node = tree.cursor_node
            if node is None or not getattr(node, "data", None):
                self.bell()
                return
            leaf = self._state.leaves.get(str(node.data))
            if leaf is None or leaf.outcome != "failed":
                self.bell()
                return
            self.push_screen(FailureDetailScreen(leaf, self._report_html))

        def action_open_html_report(self) -> None:
            if not self._report_html.is_file():
                self.bell()
                return
            try:
                # macOS + Linux cover — falls through silently on failure.
                opener = "open" if sys.platform == "darwin" else "xdg-open"
                subprocess.Popen([opener, str(self._report_html)])  # noqa: S603,S607
            except Exception:
                self.bell()

        def action_cycle_fwlog_filter(self) -> None:
            """Cycle the firmware-log port filter: None → port1 → port2 → … → None."""
            if not self._fwlog_ports:
                self.bell()
                return
            cycle = [None, *self._fwlog_ports]
            try:
                idx = cycle.index(self._fwlog_filter)
            except ValueError:
                idx = 0
            self._fwlog_filter = cycle[(idx + 1) % len(cycle)]
            try:
                self.query_one("#fwlog-header", tx.Static).update(
                    self._fwlog_header_text()
                )
            except Exception:
                pass

        def action_coverage_panel(self) -> None:
            self.push_screen(CoverageModal(self._tool_coverage))

        def action_export_reproducer(self) -> None:
            """Key `x`: export a reproducer bundle for the focused failed test.

            Only fires when the tree cursor is on a leaf that we've seen
            fail (has a TestReport with outcome=failed). Anything else
            bells + no-ops so we don't write empty bundles.
            """
            tree = self.query_one("#test-tree", tx.Tree)
            node = tree.cursor_node
            if node is None or not getattr(node, "data", None):
                self.bell()
                return
            leaf = self._state.leaves.get(str(node.data))
            if leaf is None or leaf.outcome != "failed":
                self.bell()
                return
            # Snapshot current device state into the bundle so the
            # receiving human has the same context you had when exporting.
            device_rows: list[dict[str, Any]] = []
            for row in self._state.devices:
                device_rows.append(
                    {
                        "role": row.role,
                        "port": row.port,
                        "vid": row.vid,
                        "pid": row.pid,
                        "description": row.description,
                        "info": row.info,
                    }
                )
            ctx = _reproducer_mod.ReproContext(
                nodeid=leaf.nodeid,
                longrepr=leaf.longrepr,
                sections=list(leaf.sections),
                start_ts=leaf.start_ts,
                stop_ts=leaf.stop_ts,
                seed=self._seed,
                run_number=self._run_number,
                exit_code=self._state.exit_code,
                fwlog_path=self._fwlog,
                output_dir=self._repro_dir,
                extra_device_rows=device_rows,
            )
            try:
                archive = _reproducer_mod.build_reproducer_bundle(ctx)
            except Exception as exc:
                self.push_screen(
                    ReproducerResultModal(pathlib.Path("(none)"), error=repr(exc))
                )
                return
            self.push_screen(ReproducerResultModal(archive))

        def action_quit_app(self) -> None:
            # First press: initiate graceful shutdown of the pgroup.
            # Second press within 2 s: hard-kill.
            if self._proc is None or self._proc.poll() is not None:
                self._cleanup_and_exit()
                return
            self._sigint_count += 1
            if self._sigint_count == 1:
                try:
                    os.killpg(self._proc.pid, signal.SIGINT)
                except ProcessLookupError:
                    pass
                log = self.query_one("#pytest-log", tx.RichLog)
                log.write("[tui] sent SIGINT; waiting up to 5 s for graceful exit…")
                # Escalator thread
                threading.Thread(target=self._escalate_kill, daemon=True).start()
            else:
                self._hard_kill()

        def _escalate_kill(self) -> None:
            deadline = time.monotonic() + _SIGINT_GRACE_S
            while time.monotonic() < deadline:
                if self._proc is None or self._proc.poll() is not None:
                    self.call_from_thread(self._cleanup_and_exit)
                    return
                time.sleep(0.1)
            if self._proc is not None and self._proc.poll() is None:
                try:
                    os.killpg(self._proc.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            deadline = time.monotonic() + _SIGTERM_GRACE_S
            while time.monotonic() < deadline:
                if self._proc is None or self._proc.poll() is not None:
                    self.call_from_thread(self._cleanup_and_exit)
                    return
                time.sleep(0.1)
            self._hard_kill()

        def _hard_kill(self) -> None:
            if self._proc is not None and self._proc.poll() is None:
                try:
                    os.killpg(self._proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            self.call_from_thread(self._cleanup_and_exit)

        def _cleanup_and_exit(self) -> None:
            self._stop.set()
            self.exit(return_code=self._state.exit_code or 0)

        def _reset_for_rerun(self) -> None:
            """Clear counters + tree + leaves for a focused re-run."""
            with self._state.lock:
                self._state.leaves.clear()
                self._state.nodeid_order.clear()
                for t in self._state.tiers.values():
                    t.passed = t.failed = t.skipped = t.running = t.remaining = 0
                self._state.exit_code = None
                # Defensive: the call-event handler would have cleared this
                # at the end of the prior run, but if the prior run was
                # interrupted (SIGINT during a test body) it may linger.
                self._state.running_nodeid = None
                self._state.running_started_at = None
            # Device status cells need to go back to "idle" — otherwise
            # the prior run's RUNNING: marker sticks until the next test
            # actually starts.
            self._refresh_device_status()
            # Reset UI
            tier_table = self.query_one("#tier-table", tx.DataTable)
            for t in TIERS:
                tier_table.update_cell(t, column_key="Passed", value="0")
                tier_table.update_cell(t, column_key="Failed", value="0")
                tier_table.update_cell(t, column_key="Skipped", value="0")
                tier_table.update_cell(t, column_key="Running", value="0")
                tier_table.update_cell(t, column_key="Remaining", value="0")
                tier_table.update_cell(t, column_key="Progress", value="")
            tree = self.query_one("#test-tree", tx.Tree)
            tree.root.remove_children()
            log = self.query_one("#pytest-log", tx.RichLog)
            log.write("")
            log.write("[tui] --- re-run ---")
            # Clear the fwlog pane too — it's fresh context for the new run.
            try:
                self.query_one("#fwlog-log", tx.RichLog).clear()
            except Exception:
                pass
            # Reset fwlog filter state; the conftest truncates fwlog.jsonl
            # on fixture setup, but we also unlink here so our tail worker
            # sees the new file from byte 0.
            self._fwlog_filter = None
            self._fwlog_ports = []
            try:
                self.query_one("#fwlog-header", tx.Static).update(
                    self._fwlog_header_text()
                )
            except Exception:
                pass
            self._start_time = time.monotonic()
            for p in (self._reportlog, self._fwlog):
                try:
                    p.unlink(missing_ok=True)
                except Exception:
                    pass

    return TestTuiApp()


if __name__ == "__main__":
    sys.exit(main())
