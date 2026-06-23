"""Thin async wrapper over a single aiosqlite connection.

``row_factory`` is set to ``aiosqlite.Row`` so every fetch behaves like a dict
(``row["col"]``); the ``repo_*`` modules copy rows into plain dicts before
handing them back so callers can add computed keys.
"""

from __future__ import annotations

import os
from pathlib import Path

import aiosqlite

# --- schema -----------------------------------------------------------------
# One file, created on connect. Plain `IF NOT EXISTS` — the registry is a cache
# of discovered hardware and recorded history, not a source of truth, so a
# wipe-and-rediscover is always safe.
SCHEMA = """
CREATE TABLE IF NOT EXISTS devices (
    serial_number    TEXT PRIMARY KEY,
    node_num         INTEGER,
    friendly_name    TEXT,
    hw_model         TEXT,
    vid              TEXT,
    pid              TEXT,
    role             TEXT,
    current_port     TEXT,
    firmware_version TEXT,
    region           TEXT,
    env              TEXT,
    env_locked       INTEGER NOT NULL DEFAULT 0,
    flashed_fw_branch TEXT,
    flashed_fw_sha   TEXT,
    flashed_at       REAL,
    online           INTEGER NOT NULL DEFAULT 0,
    first_seen       REAL NOT NULL DEFAULT 0,
    last_seen        REAL NOT NULL DEFAULT 0,
    kind             TEXT NOT NULL DEFAULT 'usb',   -- 'usb' | 'native'
    tcp_port         INTEGER
);

CREATE TABLE IF NOT EXISTS cameras (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    name         TEXT NOT NULL,
    type         TEXT NOT NULL DEFAULT 'usb',
    device_index TEXT,
    backend      TEXT,
    rotation     INTEGER NOT NULL DEFAULT 0,
    enabled      INTEGER NOT NULL DEFAULT 1,
    created_at   REAL NOT NULL DEFAULT 0,
    device_serial TEXT,
    assigned_at  REAL
);

CREATE TABLE IF NOT EXISTS flash_events (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    device_serial TEXT NOT NULL,
    env           TEXT,
    fw_sha        TEXT,
    from_artifact INTEGER NOT NULL DEFAULT 0,
    duration_s    REAL,
    ok            INTEGER NOT NULL DEFAULT 1,
    ts            REAL NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS runs (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    started_at  REAL NOT NULL DEFAULT 0,
    finished_at REAL,
    exit_code   INTEGER,
    args        TEXT,
    seed        TEXT,
    fw_branch   TEXT,
    fw_sha      TEXT,
    fw_dirty    INTEGER NOT NULL DEFAULT 0,
    passed      INTEGER NOT NULL DEFAULT 0,
    failed      INTEGER NOT NULL DEFAULT 0,
    skipped     INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS results (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id        INTEGER NOT NULL,
    nodeid        TEXT NOT NULL,
    tier          TEXT,
    outcome       TEXT,
    duration_s    REAL,
    device_serial TEXT,
    longrepr      TEXT,
    ts            REAL NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_results_device ON results(device_serial);
CREATE INDEX IF NOT EXISTS idx_results_run ON results(run_id);

CREATE TABLE IF NOT EXISTS builds (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    env         TEXT NOT NULL,
    fw_sha      TEXT NOT NULL,
    fw_branch   TEXT,
    status      TEXT NOT NULL DEFAULT 'queued',
    duration_s  REAL,
    artifact_dir TEXT,
    error       TEXT,
    created_at  REAL NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_builds_key ON builds(env, fw_sha);

CREATE TABLE IF NOT EXISTS settings (
    key   TEXT PRIMARY KEY,
    value TEXT
);
"""


def default_db_path() -> Path:
    """Where the registry lives for a normal (non-test) run."""
    env = os.environ.get("MESHTASTIC_MCP_WEB_DB")
    if env:
        return Path(env)
    return Path.home() / ".meshtastic_mcp" / "fleetsuite.db"


class Database:
    """Owns one aiosqlite connection. ``await connect()`` before use, and
    ``await close()`` when done. Helpers commit after every write — the access
    pattern is low-volume bench traffic, not a hot loop."""

    def __init__(self, path: Path | str) -> None:
        self.path = Path(path)
        self._conn: aiosqlite.Connection | None = None

    async def connect(self) -> "Database":
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._conn = await aiosqlite.connect(str(self.path))
        self._conn.row_factory = aiosqlite.Row
        await self._conn.execute("PRAGMA journal_mode=WAL;")
        await self._conn.executescript(SCHEMA)
        await self._conn.commit()
        return self

    async def close(self) -> None:
        if self._conn is not None:
            await self._conn.close()
            self._conn = None

    @property
    def conn(self) -> aiosqlite.Connection:
        if self._conn is None:
            raise RuntimeError("Database.connect() not called")
        return self._conn

    async def execute(self, sql: str, params: tuple = ()) -> aiosqlite.Cursor:
        cur = await self.conn.execute(sql, params)
        await self.conn.commit()
        return cur

    async def fetchone(self, sql: str, params: tuple = ()):
        async with self.conn.execute(sql, params) as cur:
            return await cur.fetchone()

    async def fetchall(self, sql: str, params: tuple = ()):
        async with self.conn.execute(sql, params) as cur:
            return await cur.fetchall()
