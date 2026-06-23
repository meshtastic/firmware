"""Datadog forwarder.

Pure mappers (``log_to_dd`` / ``telemetry_to_metrics``) turn recorder rows into
dashboard-compatible Datadog payloads; ``_read_live`` is a cursor-based tail of
the recorder's JSONL streams; ``DDConfig`` persists settings (with the API key
masked on the way out). The shipping loop lives in ``DDForwarder``.
"""

from __future__ import annotations

import json
import os
import re
from dataclasses import asdict, dataclass, fields
from pathlib import Path

import logging

from ..db import repo_settings as rs
from .scrub import Scrubber

log = logging.getLogger("meshtastic_mcp.web.datadog")

_ANSI = re.compile(r"\x1b\[[0-9;]*m")

_DDSOURCE = "meshtastic-firmware"

_STATUS = {
    "ERROR": "error",
    "CRIT": "error",
    "CRITICAL": "error",
    "WARN": "warning",
    "WARNING": "warning",
    "INFO": "info",
    "DEBUG": "debug",
    "TRACE": "debug",
    "HEAP": "debug",
}


def _strip_ansi(s: str) -> str:
    return _ANSI.sub("", s or "")


def _status_for(level: str | None) -> str:
    if not level:
        # Un-leveled output is a panic/backtrace — treat as error.
        return "error"
    return _STATUS.get(level.upper(), "info")


# --- log mapping ------------------------------------------------------------
def log_to_dd(
    rec: dict,
    *,
    host: str,
    base_tags: list[str],
    port_tags: dict[str, list[str]],
    scrubber: Scrubber,
    ship_debug: bool,
) -> dict | None:
    """Map a recorder log row to a Datadog log intake payload.

    Returns None for a DEBUG line when ``ship_debug`` is False. Un-leveled lines
    (panics/backtraces) always ship.
    """
    level = rec.get("level")
    if level and level.upper() == "DEBUG" and not ship_debug:
        return None

    message = scrubber.scrub(_strip_ansi(rec.get("line", "")))

    tags = list(base_tags)
    port = rec.get("port")
    if port:
        tags.append(f"port:{port}")
        tags.extend(port_tags.get(port, []))
    if level:
        tags.append(f"level:{level.lower()}")
    tag = rec.get("tag")
    if tag:
        tags.append(f"thread:{tag.lower()}")

    payload = {
        "ddsource": _DDSOURCE,
        "service": _DDSOURCE,
        "hostname": host,
        "message": message,
        "ddtags": ",".join(tags),
        "status": _status_for(level),
    }
    if level is not None:
        payload["level"] = level
    if rec.get("ts") is not None:
        payload["timestamp"] = int(round(rec["ts"] * 1000))
    if rec.get("heap_free") is not None:
        payload["heap_free"] = rec["heap_free"]
    return payload


# --- metric mapping ---------------------------------------------------------
def telemetry_to_metrics(
    rec: dict,
    *,
    host: str,
    base_tags: list[str],
    port_tags: dict[str, list[str]],
) -> list[dict]:
    """Map a telemetry row to Datadog GAUGE series (one per numeric field).
    Boolean and string fields are dropped."""
    variant = rec.get("variant", "")
    fields_ = rec.get("fields", {}) or {}
    ts = int(rec.get("ts", 0) or 0)

    tags = list(base_tags) + [f"variant:{variant}"]
    port = rec.get("port")
    if port:
        tags.extend(port_tags.get(port, []))

    out: list[dict] = []
    for key, value in fields_.items():
        # bool is a subclass of int — exclude it explicitly.
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            continue
        out.append(
            {
                "metric": f"mesh.{variant}.{key}",
                "type": 3,  # GAUGE
                "points": [{"timestamp": ts, "value": float(value)}],
                "resources": [{"type": "host", "name": host}],
                "tags": list(tags),
            }
        )
    return out


# --- cursor-based JSONL tail ------------------------------------------------
def _read_live(path: Path, cursor: dict, max_lines: int) -> tuple[list[dict], dict]:
    """Read newly-appended complete JSON lines from ``path``.

    ``cursor`` is ``{"ino": int, "pos": int}``. A partial trailing line (no
    newline yet) is left for the next cycle. A cursor whose inode no longer
    matches, or whose position is past EOF (rotation/truncation), resets to the
    start. Returns ``(rows, next_cursor)``.
    """
    path = Path(path)
    try:
        st = path.stat()
    except OSError:
        return [], dict(cursor)
    ino = st.st_ino

    pos = cursor.get("pos", 0) if cursor.get("ino") == ino else 0
    if pos > st.st_size:  # truncated or rotated under us
        pos = 0

    with open(path, "rb") as fh:
        fh.seek(pos)
        data = fh.read()

    last_nl = data.rfind(b"\n")
    if last_nl == -1:
        # No complete line available — leave the cursor where it was.
        return [], {"ino": ino, "pos": pos}

    complete = data[: last_nl + 1]
    consumed = pos + len(complete)

    rows: list[dict] = []
    for raw in complete.split(b"\n"):
        if not raw.strip():
            continue
        try:
            rows.append(json.loads(raw))
        except ValueError:
            continue
        if len(rows) >= max_lines:
            break
    return rows, {"ino": ino, "pos": consumed}


# --- intake host derivation -------------------------------------------------
def _browser_intake_origin(site: str) -> str:
    """The RUM/browser-logs intake origin for a Datadog site.

    ``us5.datadoghq.com`` → ``https://browser-intake-us5-datadoghq.com``
    ``datadoghq.eu``      → ``https://browser-intake-datadoghq.eu``
    """
    head, _, tld = site.rpartition(".")
    head = head.replace(".", "-")
    return f"https://browser-intake-{head}.{tld}" if head else f"https://browser-intake-{tld}"


def _logs_intake_url(site: str) -> str:
    return f"https://http-intake.logs.{site}/api/v2/logs"


def _metrics_intake_url(site: str) -> str:
    return f"https://api.{site}/api/v2/series"


# --- config -----------------------------------------------------------------
@dataclass
class DDConfig:
    enabled: bool = False
    api_key: str = ""
    site: str = "datadoghq.com"
    scrub: str = "coarse"
    collector: str = ""
    host: str = ""
    ship_debug: bool = False

    def masked(self) -> dict:
        """Config for the UI — the API key is never exposed, only a hint and a
        client-token flag (Datadog client tokens start with ``pub``)."""
        return {
            "enabled": self.enabled,
            "site": self.site,
            "scrub": self.scrub,
            "collector": self.collector,
            "host": self.host,
            "ship_debug": self.ship_debug,
            "has_key": bool(self.api_key),
            "key_hint": self.api_key[-4:] if self.api_key else "",
            "is_client_token": self.api_key.startswith("pub") if self.api_key else False,
        }

    @classmethod
    def from_dict(cls, d: dict | None) -> "DDConfig":
        d = d or {}
        allowed = {f.name for f in fields(cls)}
        return cls(**{k: v for k, v in d.items() if k in allowed})


async def load_config(db) -> DDConfig:
    return DDConfig.from_dict(await rs.get_json(db, "datadog"))


async def save_config(db, cfg: DDConfig) -> None:
    await rs.set_json(db, "datadog", asdict(cfg))


# --- forwarder (runtime) ----------------------------------------------------
def _recorder_dir() -> Path:
    return Path(os.environ.get("MESHTASTIC_MCP_RECORDER_DIR", ".mtlog"))


class DDForwarder:
    """Background loop that tails the recorder streams and ships to Datadog.
    Started/reconfigured from the ``/api/datadog`` routes; a no-op until a key
    and ``enabled`` are set."""

    def __init__(self, db, hub) -> None:
        self.db = db
        self.hub = hub
        self.cfg = DDConfig()
        self.stats = {
            "running": False,
            "sent_logs": 0,
            "sent_metrics": 0,
            "cycles": 0,
            "last_error": None,
            "last_cycle_ts": None,
        }
        self._cursors: dict[str, dict] = {}

    def status(self) -> dict:
        return {"config": self.cfg.masked(), "stats": dict(self.stats)}

    async def reload(self) -> None:
        self.cfg = await load_config(self.db)

    def test_key(self) -> dict:
        """Validate the configured API key against Datadog. Best-effort —
        returns ``{ok, error}`` and never raises."""
        if not self.cfg.api_key:
            return {"ok": False, "error": "no API key configured"}
        try:
            import requests

            resp = requests.get(
                f"https://api.{self.cfg.site}/api/v1/validate",
                headers={"DD-API-KEY": self.cfg.api_key},
                timeout=10,
            )
            if resp.ok and resp.json().get("valid"):
                return {"ok": True, "error": None}
            return {"ok": False, "error": f"validation failed ({resp.status_code})"}
        except Exception as exc:  # noqa: BLE001 - surfaced to the UI
            return {"ok": False, "error": str(exc)}
