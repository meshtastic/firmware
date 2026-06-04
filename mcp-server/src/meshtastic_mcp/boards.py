"""Board / PlatformIO env enumeration.

Parses `pio project config --json-output` — a nested list of
`[section_name, [[key, value], ...]]` pairs — into a dict keyed by env name,
extracting the `custom_meshtastic_*` metadata the firmware variants expose.

The parsed config is cached and invalidated when `platformio.ini`'s mtime
changes, so subsequent calls don't pay the 1–2s pio startup cost.
"""

from __future__ import annotations

import threading
from typing import Any

from . import config, pio

_CACHE_LOCK = threading.Lock()
_CACHE: dict[str, Any] = {"mtime": None, "envs": None}


def _parse_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.strip().lower() in ("true", "yes", "1", "on")
    return bool(value)


def _parse_int(value: Any) -> int | None:
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _parse_tags(value: Any) -> list[str]:
    if value is None:
        return []
    if isinstance(value, list):
        return [str(v).strip() for v in value if str(v).strip()]
    return [t.strip() for t in str(value).replace(",", " ").split() if t.strip()]


def _env_record(env_name: str, items: list[list[Any]]) -> dict[str, Any]:
    """Build a normalized dict for one env section."""
    d = dict(items)
    return {
        "env": env_name,
        "architecture": d.get("custom_meshtastic_architecture"),
        "hw_model": _parse_int(d.get("custom_meshtastic_hw_model")),
        "hw_model_slug": d.get("custom_meshtastic_hw_model_slug"),
        "display_name": d.get("custom_meshtastic_display_name"),
        "actively_supported": _parse_bool(
            d.get("custom_meshtastic_actively_supported")
        ),
        "support_level": _parse_int(d.get("custom_meshtastic_support_level")),
        "board_level": d.get("board_level"),  # "pr", "extra", or None
        "tags": _parse_tags(d.get("custom_meshtastic_tags")),
        "images": _parse_tags(d.get("custom_meshtastic_images")),
        "board": d.get("board"),
        "upload_speed": _parse_int(d.get("upload_speed")),
        "upload_protocol": d.get("upload_protocol"),
        "monitor_speed": _parse_int(d.get("monitor_speed")),
        "monitor_filters": d.get("monitor_filters") or [],
        "_raw": d,  # Full dict for get_board
    }


def _load_all() -> dict[str, dict[str, Any]]:
    """Parse `pio project config` into `{env_name: record}`."""
    raw = pio.run_json(["project", "config"], timeout=pio.TIMEOUT_PROJECT_CONFIG)
    result: dict[str, dict[str, Any]] = {}
    for section_name, items in raw:
        if not isinstance(section_name, str) or not section_name.startswith("env:"):
            continue
        env_name = section_name.split(":", 1)[1]
        result[env_name] = _env_record(env_name, items)
    return result


def _get_cached() -> dict[str, dict[str, Any]]:
    root = config.firmware_root()
    platformio_ini = root / "platformio.ini"
    try:
        mtime = platformio_ini.stat().st_mtime
    except FileNotFoundError:
        mtime = None

    with _CACHE_LOCK:
        if _CACHE["envs"] is not None and _CACHE["mtime"] == mtime:
            return _CACHE["envs"]
        envs = _load_all()
        _CACHE["envs"] = envs
        _CACHE["mtime"] = mtime
        return envs


def invalidate_cache() -> None:
    with _CACHE_LOCK:
        _CACHE["envs"] = None
        _CACHE["mtime"] = None


def _public_record(rec: dict[str, Any]) -> dict[str, Any]:
    """Strip the `_raw` field for list outputs."""
    return {k: v for k, v in rec.items() if not k.startswith("_")}


def list_boards(
    architecture: str | None = None,
    actively_supported_only: bool = False,
    query: str | None = None,
    board_level: str | None = None,  # "release" | "pr" | "extra"
) -> list[dict[str, Any]]:
    """Enumerate PlatformIO envs with Meshtastic metadata.

    Filters are cumulative (AND). `board_level="release"` means envs with no
    explicit `board_level` set (the default release targets).
    """
    envs = _get_cached()
    q = query.lower().strip() if query else None

    out = []
    for rec in envs.values():
        if architecture and rec.get("architecture") != architecture:
            continue
        if actively_supported_only and not rec.get("actively_supported"):
            continue
        if board_level is not None:
            rec_level = rec.get("board_level")
            if board_level == "release":
                if rec_level not in (None, ""):
                    continue
            elif rec_level != board_level:
                continue
        if q:
            display = (rec.get("display_name") or "").lower()
            env_name = rec.get("env", "").lower()
            slug = (rec.get("hw_model_slug") or "").lower()
            if q not in display and q not in env_name and q not in slug:
                continue
        out.append(_public_record(rec))

    out.sort(key=lambda r: (r.get("architecture") or "", r.get("env")))
    return out


def get_board(env: str) -> dict[str, Any]:
    """Full metadata for one env, including the raw pio config dict."""
    envs = _get_cached()
    rec = envs.get(env)
    if rec is None:
        raise KeyError(
            f"Unknown env: {env!r}. Use list_boards() to see available envs."
        )
    public = _public_record(rec)
    public["raw_config"] = rec["_raw"]
    return public
