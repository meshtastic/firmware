"""Firmware-build ledger, keyed by (env, fw_sha). The orchestrator records one
row per build attempt; ``get`` returns the latest for a key so a cache hit can
be distinguished from a fresh queue."""

from __future__ import annotations

import time

from .database import Database

_COLS = (
    "id, env, fw_sha, fw_branch, status, duration_s, artifact_dir, error, "
    "created_at"
)


def _to_dict(row) -> dict | None:
    return dict(row) if row is not None else None


async def create(
    db: Database, *, env: str, fw_sha: str, fw_branch: str | None, status: str
) -> int:
    cur = await db.execute(
        "INSERT INTO builds (env, fw_sha, fw_branch, status, created_at) "
        "VALUES (?,?,?,?,?)",
        (env, fw_sha, fw_branch, status, time.time()),
    )
    return cur.lastrowid


async def set_status(
    db: Database,
    build_id: int,
    *,
    status: str,
    duration_s: float | None = None,
    artifact_dir: str | None = None,
    error: str | None = None,
) -> dict | None:
    await db.execute(
        "UPDATE builds SET status=?, duration_s=?, artifact_dir=?, error=? "
        "WHERE id=?",
        (status, duration_s, artifact_dir, error, build_id),
    )
    return await get_by_id(db, build_id)


async def get_by_id(db: Database, build_id: int) -> dict | None:
    row = await db.fetchone(f"SELECT {_COLS} FROM builds WHERE id=?", (build_id,))
    return _to_dict(row)


async def get(db: Database, env: str, fw_sha: str) -> dict | None:
    """Latest build row for a key, or None if it was never attempted."""
    row = await db.fetchone(
        f"SELECT {_COLS} FROM builds WHERE env=? AND fw_sha=? "
        "ORDER BY id DESC LIMIT 1",
        (env, fw_sha),
    )
    return _to_dict(row)


async def list_all(db: Database, limit: int = 100) -> list[dict]:
    rows = await db.fetchall(
        f"SELECT {_COLS} FROM builds ORDER BY id DESC LIMIT ?", (limit,)
    )
    return [dict(r) for r in rows]
