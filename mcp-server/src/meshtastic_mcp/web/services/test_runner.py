"""The pytest runner.

``resolve_env_overrides`` and ``is_running`` are the pure pieces the safety
gate and the run-launcher depend on. ``TestRunner`` drives an actual pytest
subprocess: it bakes per-board env overrides, tails ``pytest-reportlog`` JSONL
for live per-test progress, and streams stdout/stderr + firmware logs over the
hub.
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import sys
import tempfile
import time
from pathlib import Path

log = logging.getLogger("meshtastic_mcp.web.test_runner")

# Tiers the UI knows about, in display order. A nodeid maps to a tier by its
# path under tests/ (directory name, or "bake"/"unit" for top-level files).
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

# Module-level run state. A single harness runs one suite at a time.
_state: dict = {"running": False, "run_id": None, "exit_code": None, "proc": None}


def is_running() -> bool:
    return bool(_state.get("running"))


def status() -> dict:
    return {
        "running": _state["running"],
        "run_id": _state["run_id"],
        "exit_code": _state["exit_code"],
    }


def resolve_env_overrides(rows: list[dict]) -> dict[str, str]:
    """From the online, env-resolved devices, bake one
    ``MESHTASTIC_MCP_ENV_<ROLE>=<env>`` override per role. Rows without a role
    or env are skipped (so native/TCP nodes never become a flash target)."""
    overrides: dict[str, str] = {}
    for row in rows:
        role = row.get("role")
        env = row.get("env")
        if not role or not env:
            continue
        overrides[f"MESHTASTIC_MCP_ENV_{role.upper()}"] = env
    return overrides


def tier_for(nodeid: str) -> str:
    """Derive a tier from a pytest nodeid path."""
    path = nodeid.split("::", 1)[0]
    parts = path.split("/")
    if "tests" in parts:
        rest = parts[parts.index("tests") + 1 :]
        if rest:
            seg = rest[0]
            if seg.endswith(".py"):
                return "bake" if "bake" in seg else "unit"
            return seg
    return "unit"


def _split_nodeid(nodeid: str) -> tuple[str, str]:
    path, _, name = nodeid.partition("::")
    return path, name or nodeid


class TestRunner:
    """Owns the live pytest subprocess + its reportlog tail. One per app."""

    def __init__(self, db, hub) -> None:
        self.db = db
        self.hub = hub
        self._task: asyncio.Task | None = None

    async def start(self, args: list[str]) -> dict:
        from . import firmware  # local import to avoid a cycle at module load

        if is_running():
            raise RuntimeError("a test run is already in progress")

        fw = firmware.firmware_ref()
        from ..db import repo_devices as rd

        overrides = resolve_env_overrides(await rd.online_with_env(self.db))

        from ..db import repo_runs as rr

        run_id = await rr.create_run(
            self.db,
            args=args,
            seed=str(int(time.time())),
            fw_branch=fw.get("branch"),
            fw_sha=fw.get("sha"),
            fw_dirty=bool(fw.get("dirty")),
        )
        _state.update(running=True, run_id=run_id, exit_code=None)
        await self.hub.publish("test.progress", {"type": "run_started", "run_id": run_id})
        self._task = asyncio.create_task(self._drive(run_id, args, overrides))
        return status()

    async def stop(self) -> None:
        proc = _state.get("proc")
        if proc and proc.returncode is None:
            try:
                proc.terminate()
            except ProcessLookupError:
                pass

    async def _drive(self, run_id: int, args: list[str], overrides: dict) -> None:
        from .. import config as _cfg  # noqa: F401
        from ..db import repo_runs as rr
        from meshtastic_mcp import config as mcfg

        exit_code = None
        report = Path(tempfile.gettempdir()) / f"fleetsuite-report-{run_id}.jsonl"
        report.unlink(missing_ok=True)
        try:
            root = mcfg.firmware_root() / "mcp-server"
        except Exception:  # noqa: BLE001
            root = Path.cwd()

        env = dict(os.environ)
        env.update(overrides)

        cmd = [
            sys.executable,
            "-m",
            "pytest",
            "-p",
            "no:cacheprovider",
            f"--report-log={report}",
            "-v",
            *args,
        ]
        try:
            proc = await asyncio.create_subprocess_exec(
                *cmd,
                cwd=str(root),
                env=env,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            _state["proc"] = proc
            tail = asyncio.create_task(self._tail_report(run_id, report))
            await asyncio.gather(
                self._pump(proc.stdout, "stdout"),
                self._pump(proc.stderr, "stderr"),
            )
            exit_code = await proc.wait()
            await asyncio.sleep(0.2)  # let the reportlog flush
            tail.cancel()
        except FileNotFoundError as exc:
            await self.hub.publish(
                "test.stdout", {"line": f"failed to launch pytest: {exc}", "source": "stderr"}
            )
            exit_code = 127
        finally:
            _state.update(running=False, exit_code=exit_code, proc=None)
            await rr.finish_run(self.db, run_id, exit_code=exit_code)
            await self.hub.publish(
                "test.progress", {"type": "run_finished", "exit_code": exit_code}
            )
            report.unlink(missing_ok=True)

    async def _pump(self, stream, source: str) -> None:
        if stream is None:
            return
        while True:
            raw = await stream.readline()
            if not raw:
                break
            line = raw.decode(errors="replace").rstrip("\n")
            await self.hub.publish("test.stdout", {"line": line, "source": source})

    async def _tail_report(self, run_id: int, report: Path) -> None:
        """Follow the reportlog JSONL and translate entries into progress frames
        + persisted results."""
        from ..db import repo_runs as rr

        seen_register: set[str] = set()
        pos = 0
        while True:
            if report.exists():
                with open(report, "rb") as fh:
                    fh.seek(pos)
                    chunk = fh.read()
                    pos = fh.tell()
                for raw in chunk.split(b"\n"):
                    if not raw.strip():
                        continue
                    try:
                        entry = json.loads(raw)
                    except ValueError:
                        continue
                    await self._handle_entry(run_id, entry, seen_register, rr)
            await asyncio.sleep(0.3)

    async def _handle_entry(self, run_id, entry, seen_register, rr) -> None:
        rtype = entry.get("$report_type")
        if rtype == "CollectReport":
            for item in entry.get("result", []):
                nodeid = item.get("nodeid")
                if not nodeid or "::" not in nodeid or nodeid in seen_register:
                    continue
                seen_register.add(nodeid)
                path, name = _split_nodeid(nodeid)
                await self.hub.publish(
                    "test.progress",
                    {
                        "type": "register",
                        "nodeid": nodeid,
                        "tier": tier_for(nodeid),
                        "file": path,
                        "testname": name,
                    },
                )
        elif rtype == "TestReport":
            nodeid = entry.get("nodeid")
            when = entry.get("when")
            outcome = entry.get("outcome")
            if when == "setup":
                await self.hub.publish(
                    "test.progress", {"type": "running", "nodeid": nodeid}
                )
            # Final outcome: the call phase normally, or a non-passed setup
            # (skip/error) that short-circuits the test.
            final = when == "call" or (when == "setup" and outcome != "passed")
            if final:
                duration = entry.get("duration")
                await self.hub.publish(
                    "test.progress",
                    {
                        "type": "outcome",
                        "nodeid": nodeid,
                        "outcome": outcome,
                        "duration": duration,
                    },
                )
                longrepr = entry.get("longrepr")
                await rr.add_result(
                    self.db,
                    run_id,
                    nodeid=nodeid,
                    tier=tier_for(nodeid or ""),
                    outcome=outcome,
                    duration_s=duration,
                    device_serial=None,
                    longrepr=str(longrepr) if longrepr else None,
                )
