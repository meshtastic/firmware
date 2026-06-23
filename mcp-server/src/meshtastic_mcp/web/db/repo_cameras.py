"""Camera registry. A camera is an independent entity (a USB capture device)
that can be *assigned* to a device serial; rotation is a property of the camera
mount, so it survives reassignment."""

from __future__ import annotations

import time

from .database import Database

_COLS = (
    "id, name, type, device_index, backend, rotation, mirror, enabled, "
    "created_at, device_serial, assigned_at"
)


def _to_dict(row) -> dict | None:
    return dict(row) if row is not None else None


def _normalize_rotation(rotation: int) -> int:
    """Snap to the nearest quarter-turn and wrap into [0, 360)."""
    return int(round((rotation % 360) / 90.0)) * 90 % 360


async def add(db: Database, *, name: str, device_index: str) -> int:
    cur = await db.execute(
        "INSERT INTO cameras (name, type, device_index, rotation, enabled, "
        "created_at) VALUES (?, 'usb', ?, 0, 1, ?)",
        (name, device_index, time.time()),
    )
    return cur.lastrowid


async def get(db: Database, cid: int) -> dict | None:
    row = await db.fetchone(f"SELECT {_COLS} FROM cameras WHERE id=?", (cid,))
    return _to_dict(row)


async def list_all(db: Database) -> list[dict]:
    rows = await db.fetchall(f"SELECT {_COLS} FROM cameras ORDER BY id")
    return [dict(r) for r in rows]


async def remove(db: Database, cid: int) -> None:
    await db.execute("DELETE FROM cameras WHERE id=?", (cid,))


async def assign(db: Database, cid: int, device_serial: str | None) -> dict | None:
    await db.execute(
        "UPDATE cameras SET device_serial=?, assigned_at=? WHERE id=?",
        (device_serial, time.time() if device_serial else None, cid),
    )
    return await get(db, cid)


async def for_device(db: Database, serial: str) -> dict | None:
    row = await db.fetchone(
        f"SELECT {_COLS} FROM cameras WHERE device_serial=? LIMIT 1", (serial,)
    )
    return _to_dict(row)


async def set_rotation(db: Database, cid: int, rotation: int) -> dict | None:
    await db.execute(
        "UPDATE cameras SET rotation=? WHERE id=?",
        (_normalize_rotation(rotation), cid),
    )
    return await get(db, cid)


async def set_mirror(db: Database, cid: int, mirror: bool) -> dict | None:
    """Horizontal flip — a property of the camera mount, like rotation."""
    await db.execute(
        "UPDATE cameras SET mirror=? WHERE id=?", (int(bool(mirror)), cid)
    )
    return await get(db, cid)


async def set_backend(db: Database, cid: int, backend: str | None) -> None:
    await db.execute("UPDATE cameras SET backend=? WHERE id=?", (backend, cid))
