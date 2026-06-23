"""Test-run history. A run is a single pytest invocation; results are the
per-test leaves, optionally attributed to the device they exercised."""

from __future__ import annotations

import json
import time

from .database import Database

_RUN_COLS = (
    "id, started_at, finished_at, exit_code, args, seed, fw_branch, fw_sha, "
    "fw_dirty, passed, failed, skipped"
)


def _run_to_dict(row) -> dict | None:
    if row is None:
        return None
    d = dict(row)
    try:
        d["args"] = json.loads(d["args"]) if d.get("args") else []
    except (ValueError, TypeError):
        d["args"] = []
    return d


async def create_run(
    db: Database,
    *,
    args: list[str],
    seed: str | None,
    fw_branch: str | None,
    fw_sha: str | None,
    fw_dirty: bool,
) -> int:
    cur = await db.execute(
        "INSERT INTO runs (started_at, args, seed, fw_branch, fw_sha, fw_dirty) "
        "VALUES (?,?,?,?,?,?)",
        (time.time(), json.dumps(args or []), seed, fw_branch, fw_sha, int(fw_dirty)),
    )
    return cur.lastrowid


async def get_run(db: Database, run_id: int) -> dict | None:
    row = await db.fetchone(f"SELECT {_RUN_COLS} FROM runs WHERE id=?", (run_id,))
    return _run_to_dict(row)


async def list_runs(db: Database, limit: int = 50) -> list[dict]:
    rows = await db.fetchall(
        f"SELECT {_RUN_COLS} FROM runs ORDER BY id DESC LIMIT ?", (limit,)
    )
    return [_run_to_dict(r) for r in rows]


async def finish_run(db: Database, run_id: int, *, exit_code: int | None) -> None:
    await db.execute(
        "UPDATE runs SET finished_at=?, exit_code=? WHERE id=?",
        (time.time(), exit_code, run_id),
    )


async def add_result(
    db: Database,
    run_id: int,
    *,
    nodeid: str,
    tier: str | None,
    outcome: str,
    duration_s: float | None,
    device_serial: str | None,
    longrepr: str | None,
) -> None:
    await db.execute(
        "INSERT INTO results "
        "(run_id, nodeid, tier, outcome, duration_s, device_serial, longrepr, ts) "
        "VALUES (?,?,?,?,?,?,?,?)",
        (run_id, nodeid, tier, outcome, duration_s, device_serial, longrepr, time.time()),
    )
    col = {"passed": "passed", "failed": "failed", "skipped": "skipped"}.get(outcome)
    if col:
        await db.execute(
            f"UPDATE runs SET {col}={col}+1 WHERE id=?", (run_id,)
        )


async def results_for_device(db: Database, serial: str) -> list[dict]:
    """Every recorded result for a device, newest first, joined to its run so
    each row carries the firmware sha it ran against."""
    rows = await db.fetchall(
        "SELECT r.id, r.run_id, r.nodeid, r.tier, r.outcome, r.duration_s, "
        "r.device_serial, r.longrepr, r.ts, runs.fw_sha, runs.fw_branch "
        "FROM results r JOIN runs ON r.run_id = runs.id "
        "WHERE r.device_serial=? ORDER BY r.id DESC",
        (serial,),
    )
    return [dict(r) for r in rows]
