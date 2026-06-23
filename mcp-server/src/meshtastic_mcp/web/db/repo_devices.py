"""Device registry — the heart of the harness's identity model.

A device is keyed by its USB serial number so its row (and everything attached
to it — friendly name, camera, pinned env, flash/test history) *follows* it
across ports and reboots. Discovery never deletes rows; it only flips ``online``
so a unplugged device greys out instead of vanishing.
"""

from __future__ import annotations

import time

from ..services import identity
from .database import Database

_COLS = (
    "serial_number, node_num, friendly_name, hw_model, vid, pid, role, "
    "current_port, firmware_version, region, env, env_locked, "
    "flashed_fw_branch, flashed_fw_sha, flashed_at, online, first_seen, "
    "last_seen, kind, tcp_port"
)


def _to_dict(row) -> dict | None:
    if row is None:
        return None
    d = dict(row)
    serial = d.get("serial_number") or ""
    d["has_stable_id"] = identity.has_stable_id(serial)
    # A device is "stale" once it has gone offline — the card stays but greys.
    d["stale"] = not bool(d.get("online"))
    return d


async def get(db: Database, serial: str) -> dict | None:
    row = await db.fetchone(
        f"SELECT {_COLS} FROM devices WHERE serial_number=?", (serial,)
    )
    return _to_dict(row)


async def list_all(db: Database) -> list[dict]:
    rows = await db.fetchall(f"SELECT {_COLS} FROM devices ORDER BY first_seen")
    return [_to_dict(r) for r in rows]


async def upsert_from_discovery(
    db: Database,
    *,
    serial_number: str,
    current_port: str,
    vid: str | None,
    pid: str | None,
    role: str | None,
) -> dict:
    """Insert a freshly-seen device or refresh an existing one.

    Returns the device dict with two transient flags the discovery loop uses to
    decide what to broadcast: ``_is_new`` and ``_port_changed``. Never clobbers
    operator-owned fields (friendly_name, pinned env) on re-discovery.
    """
    now = time.time()
    existing = await get(db, serial_number)
    if existing is None:
        await db.execute(
            "INSERT INTO devices "
            "(serial_number, current_port, vid, pid, role, kind, online, "
            " first_seen, last_seen, env_locked) "
            "VALUES (?,?,?,?,?,'usb',1,?,?,0)",
            (serial_number, current_port, vid, pid, role, now, now),
        )
        row = await get(db, serial_number)
        row["_is_new"] = True
        row["_port_changed"] = False
        return row

    port_changed = existing["current_port"] != current_port
    await db.execute(
        "UPDATE devices SET current_port=?, vid=?, pid=?, role=?, online=1, "
        "last_seen=? WHERE serial_number=?",
        (current_port, vid, pid, role, now, serial_number),
    )
    row = await get(db, serial_number)
    row["_is_new"] = False
    row["_port_changed"] = port_changed
    return row


async def upsert_native(
    db: Database, *, name: str, tcp_port: int, online: bool = True
) -> dict:
    """A Docker ``meshtasticd`` node — surfaced as a device with a synthetic
    ``native:<name>`` serial so the same UI/card machinery applies."""
    now = time.time()
    serial = f"native:{name}"
    port = f"tcp://127.0.0.1:{tcp_port}"
    existing = await get(db, serial)
    if existing is None:
        await db.execute(
            "INSERT INTO devices "
            "(serial_number, friendly_name, role, current_port, kind, "
            " tcp_port, online, first_seen, last_seen, env_locked) "
            "VALUES (?,?, 'native', ?, 'native', ?, ?, ?, ?, 0)",
            (serial, name, port, tcp_port, int(online), now, now),
        )
    else:
        await db.execute(
            "UPDATE devices SET tcp_port=?, current_port=?, online=?, "
            "last_seen=? WHERE serial_number=?",
            (tcp_port, port, int(online), now, serial),
        )
    return await get(db, serial)


async def set_friendly_name(db: Database, serial: str, name: str) -> dict | None:
    await db.execute(
        "UPDATE devices SET friendly_name=? WHERE serial_number=?", (name, serial)
    )
    return await get(db, serial)


async def set_env(
    db: Database, serial: str, env: str | None, *, locked: bool
) -> dict | None:
    """Pin (``locked=True``) or release (``locked=False``) the pio env. A pinned
    env is protected from auto-enrichment; releasing it lets detection win."""
    await db.execute(
        "UPDATE devices SET env=?, env_locked=? WHERE serial_number=?",
        (env, int(locked), serial),
    )
    return await get(db, serial)


async def update_enrichment(
    db: Database,
    serial: str,
    *,
    node_num: int | None = None,
    env: str | None = None,
    hw_model: str | None = None,
    firmware_version: str | None = None,
    region: str | None = None,
) -> dict | None:
    """Apply data read off a connected device. ``env`` is only written when the
    operator has NOT pinned one (``env_locked=0``); the rest always apply."""
    row = await get(db, serial)
    if row is None:
        return None

    sets = ["node_num=COALESCE(?, node_num)"]
    params: list = [node_num]
    for col, val in (
        ("hw_model", hw_model),
        ("firmware_version", firmware_version),
        ("region", region),
    ):
        sets.append(f"{col}=COALESCE(?, {col})")
        params.append(val)
    if env is not None and not row["env_locked"]:
        sets.append("env=?")
        params.append(env)

    params.append(serial)
    await db.execute(
        f"UPDATE devices SET {', '.join(sets)} WHERE serial_number=?", tuple(params)
    )
    return await get(db, serial)


async def record_flashed(
    db: Database, serial: str, *, branch: str | None, sha: str | None
) -> None:
    await db.execute(
        "UPDATE devices SET flashed_fw_branch=?, flashed_fw_sha=?, flashed_at=? "
        "WHERE serial_number=?",
        (branch, sha, time.time(), serial),
    )


async def mark_offline_except(db: Database, keep: set[str]) -> list[str]:
    """Flip every currently-online device not in ``keep`` to offline. Returns
    the serials that *transitioned* (so the caller can broadcast just those)."""
    rows = await db.fetchall("SELECT serial_number FROM devices WHERE online=1")
    newly = [r["serial_number"] for r in rows if r["serial_number"] not in keep]
    for serial in newly:
        await db.execute(
            "UPDATE devices SET online=0 WHERE serial_number=?", (serial,)
        )
    return newly


async def online_by_role(db: Database, role: str) -> dict | None:
    row = await db.fetchone(
        f"SELECT {_COLS} FROM devices WHERE online=1 AND role=? "
        "ORDER BY last_seen DESC LIMIT 1",
        (role,),
    )
    return _to_dict(row)


async def online_with_env(db: Database) -> list[dict]:
    """Online, real (USB) devices that have a resolved env — the candidates the
    test runner bakes per-board variant overrides from. Native nodes (no flash
    target) and un-enriched devices (no env yet) are excluded."""
    rows = await db.fetchall(
        f"SELECT {_COLS} FROM devices "
        "WHERE online=1 AND kind='usb' AND env IS NOT NULL"
    )
    return [_to_dict(r) for r in rows]
