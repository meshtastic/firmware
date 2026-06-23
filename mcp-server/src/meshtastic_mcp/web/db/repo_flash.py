"""Flash-timing log. Each flash records whether it came from a prebuilt
artifact or a from-scratch host rebuild, so the UI can show how much the
artifact cache saves on a given device."""

from __future__ import annotations

import time

from .database import Database

_COLS = "id, device_serial, env, fw_sha, from_artifact, duration_s, ok, ts"


async def record(
    db: Database,
    *,
    device_serial: str,
    env: str | None,
    fw_sha: str | None,
    from_artifact: bool,
    duration_s: float,
    ok: bool,
) -> None:
    await db.execute(
        "INSERT INTO flash_events "
        "(device_serial, env, fw_sha, from_artifact, duration_s, ok, ts) "
        "VALUES (?,?,?,?,?,?,?)",
        (
            device_serial,
            env,
            fw_sha,
            int(from_artifact),
            duration_s,
            int(ok),
            time.time(),
        ),
    )


async def _latest(db: Database, serial: str, from_artifact: bool) -> dict | None:
    row = await db.fetchone(
        f"SELECT {_COLS} FROM flash_events "
        "WHERE device_serial=? AND from_artifact=? AND ok=1 "
        "ORDER BY ts DESC LIMIT 1",
        (serial, int(from_artifact)),
    )
    return dict(row) if row is not None else None


async def comparison(db: Database, serial: str) -> dict:
    """Latest successful artifact-flash vs latest successful host-rebuild flash,
    plus the speedup ratio when both exist."""
    artifact = await _latest(db, serial, True)
    rebuild = await _latest(db, serial, False)
    speedup = None
    if artifact and rebuild and artifact["duration_s"]:
        speedup = round(rebuild["duration_s"] / artifact["duration_s"], 1)
    return {"artifact": artifact, "rebuild": rebuild, "speedup": speedup}
