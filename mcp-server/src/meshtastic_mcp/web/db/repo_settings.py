"""Tiny key/value store for JSON-serialisable config blobs (e.g. the Datadog
forwarder settings)."""

from __future__ import annotations

import json

from .database import Database


async def set_json(db: Database, key: str, obj) -> None:
    await db.execute(
        "INSERT INTO settings (key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
        (key, json.dumps(obj)),
    )


async def get_json(db: Database, key: str) -> dict | None:
    row = await db.fetchone("SELECT value FROM settings WHERE key=?", (key,))
    if row is None or row["value"] is None:
        return None
    try:
        return json.loads(row["value"])
    except (ValueError, TypeError):
        return None
