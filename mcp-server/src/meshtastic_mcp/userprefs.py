"""USERPREFS: build-time constants baked into the firmware binary.

The firmware repo has `userPrefs.jsonc` at its root — a JSONC file with every
available USERPREFS_* key listed, most commented out. At build time,
`bin/platformio-custom.py` reads it, strips comments, and emits
`-DUSERPREFS_<KEY>=<value>` build flags into the compile step. Firmware code
uses `#ifdef USERPREFS_<KEY>` to pick up the baked-in defaults for channels,
owner name, LoRa region, OEM branding, MQTT credentials, etc.

This module:
  1. Parses `userPrefs.jsonc` (preserving which keys are active vs commented)
  2. Greps `src/` for the set of keys the firmware actually consumes (the
     real discovery manifest — anything here that isn't in the jsonc is still
     a valid override)
  3. Provides a context manager for temporarily swapping in overrides during
     a build/flash, then restoring the original file
  4. Provides persistent `set` / `reset` for when the caller wants the change
     to stick across multiple builds

The firmware's platformio-custom.py value-type detection mirrors what we need
for serialization: dict-like `{...}` (byte arrays, enum lists), digit-like
(ints and floats), `true`/`false`, `meshtastic_*` enum constants, and
everything else gets string-wrapped via `env.StringifyMacro`. We store the
raw string values exactly as they'd appear in the jsonc to avoid round-trip
surprises.
"""

from __future__ import annotations

import json
import re
import shutil
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator

from . import config

USERPREFS_FILE = "userPrefs.jsonc"
BACKUP_SUFFIX = ".mcp.bak"

# Pattern for lines like `// "USERPREFS_FOO": "value",` or `"USERPREFS_FOO": "v"`
_ACTIVE_LINE = re.compile(r'^\s*"(USERPREFS_[A-Z0-9_]+)"\s*:\s*"((?:[^"\\]|\\.)*)"')
_COMMENTED_LINE = re.compile(
    r'^\s*//\s*"(USERPREFS_[A-Z0-9_]+)"\s*:\s*"((?:[^"\\]|\\.)*)"'
)
# Inline comment stripper (matches platformio-custom.py:219)
_LINE_COMMENT = re.compile(r"//.*")

# USERPREFS_* usage in firmware source (#ifdef, #if defined, direct refs)
_USAGE_PATTERN = re.compile(r"\bUSERPREFS_[A-Z0-9_]+\b")


def jsonc_path() -> Path:
    return config.firmware_root() / USERPREFS_FILE


def _read_file(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _parse_jsonc_state(text: str) -> dict[str, Any]:
    """Parse userPrefs.jsonc while preserving comment state per key.

    Returns:
        {
          "active": {key: string_value, ...},   # uncommented
          "commented": {key: string_value, ...}, # commented examples
          "order": [key, ...]                   # source order for round-trip
        }

    """
    active: dict[str, str] = {}
    commented: dict[str, str] = {}
    order: list[str] = []
    for line in text.splitlines():
        if m := _COMMENTED_LINE.match(line):
            key, val = m.group(1), m.group(2)
            commented[key] = val
            order.append(key)
        elif m := _ACTIVE_LINE.match(line):
            key, val = m.group(1), m.group(2)
            active[key] = val
            order.append(key)
    return {"active": active, "commented": commented, "order": order}


def _parse_jsonc_active(text: str) -> dict[str, str]:
    """Parse active-only values by stripping line comments + feeding to json."""
    stripped = "\n".join(_LINE_COMMENT.sub("", line) for line in text.splitlines())
    try:
        return {k: str(v) for k, v in json.loads(stripped).items()}
    except json.JSONDecodeError as exc:
        raise ValueError(f"userPrefs.jsonc is not valid JSONC: {exc}") from exc


def read_state() -> dict[str, Any]:
    """Return {active, commented, order, path}."""
    path = jsonc_path()
    if not path.is_file():
        return {"active": {}, "commented": {}, "order": [], "path": str(path)}
    state = _parse_jsonc_state(_read_file(path))
    state["path"] = str(path)
    return state


# ---------- Manifest ------------------------------------------------------


def _scan_consumed_keys() -> dict[str, list[str]]:
    """Grep firmware src/ for USERPREFS_* references.

    Returns {key: [relative_file_paths]} — only includes files under `src/`.
    """
    src_dir = config.firmware_root() / "src"
    if not src_dir.is_dir():
        return {}
    out: dict[str, set[str]] = {}
    for path in src_dir.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in {
            ".c",
            ".cc",
            ".cpp",
            ".h",
            ".hpp",
            ".ipp",
            ".inl",
        }:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue
        for m in _USAGE_PATTERN.finditer(text):
            key = m.group(0)
            # Skip our own "_USERPREFS_" artifacts (reserve-word guard from build-userprefs-json.py)
            if key.startswith("_USERPREFS_"):
                continue
            out.setdefault(key, set()).add(
                str(path.relative_to(config.firmware_root()))
            )
    return {k: sorted(v) for k, v in sorted(out.items())}


def build_manifest() -> dict[str, Any]:
    """Build the discovery manifest.

    Every known USERPREFS_* key appears exactly once with:
      - `value` (current active value, if any)
      - `example` (commented default from jsonc, if any)
      - `active` bool
      - `declared_in_jsonc` bool (key appears anywhere in userPrefs.jsonc)
      - `consumed_by` list of source files that reference it
      - `inferred_type`: one of "brace", "number", "bool", "enum", "string"
        — matches platformio-custom.py's value-wrapping switch
    """
    state = read_state()
    consumed = _scan_consumed_keys()

    all_keys = set(state["active"]) | set(state["commented"]) | set(consumed)
    records = []
    for key in sorted(all_keys):
        example = state["commented"].get(key)
        value = state["active"].get(key)
        records.append(
            {
                "key": key,
                "active": key in state["active"],
                "value": value,
                "example": example,
                "declared_in_jsonc": key in state["active"]
                or key in state["commented"],
                "consumed_by": consumed.get(key, []),
                "inferred_type": infer_type(value if value is not None else example),
            }
        )
    return {
        "path": state["path"],
        "active_count": len(state["active"]),
        "commented_count": len(state["commented"]),
        "consumed_key_count": len(consumed),
        "total_keys": len(records),
        "entries": records,
    }


def infer_type(value: str | None) -> str:
    """Classify a raw value string the way platformio-custom.py does.

    Mirrors the branch order in `bin/platformio-custom.py:222-235`.
    """
    if value is None:
        return "unknown"
    v = value.strip()
    if v.startswith("{"):
        return "brace"  # byte array / enum init list
    if v.lstrip("-").replace(".", "", 1).isdigit():
        return "number"
    if v in ("true", "false"):
        return "bool"
    if v.startswith("meshtastic_"):
        return "enum"
    return "string"


# ---------- Writing -------------------------------------------------------


def _format_jsonc_line(key: str, value: str, commented: bool) -> str:
    prefix = "  // " if commented else "  "
    # Escape backslashes and quotes inside value the way platformio-custom.py
    # expects — the original jsonc uses raw strings for most content. Keep it
    # literal; callers are responsible for correct escaping if they pass
    # dict/enum-init values that contain quotes.
    return f'{prefix}"{key}": "{value}",'


def _render_jsonc(
    active: dict[str, str], commented: dict[str, str], order: list[str]
) -> str:
    """Render userPrefs.jsonc preserving source order and comment state."""
    seen: set[str] = set()
    lines = ["{"]
    for key in order:
        if key in seen:
            continue
        seen.add(key)
        if key in active:
            lines.append(_format_jsonc_line(key, active[key], commented=False))
        elif key in commented:
            lines.append(_format_jsonc_line(key, commented[key], commented=True))
    # Append any newly-added keys (not in original order) at the end, active.
    for key, value in active.items():
        if key in seen:
            continue
        seen.add(key)
        lines.append(_format_jsonc_line(key, value, commented=False))
    # Strip trailing comma on the last data line (valid JSONC allows it, but
    # strict `json.loads` after comment-stripping does not; the loader in
    # platformio-custom.py uses json.loads).
    if len(lines) > 1 and lines[-1].endswith(","):
        lines[-1] = lines[-1].rstrip(",")
    lines.append("}")
    lines.append("")  # trailing newline
    return "\n".join(lines)


def _validate_after_write(text: str) -> None:
    """Ensure the rendered text still parses the way platformio-custom.py does."""
    stripped = "\n".join(_LINE_COMMENT.sub("", line) for line in text.splitlines())
    json.loads(stripped)  # raises on any error


def write_state(
    active: dict[str, str], commented: dict[str, str], order: list[str]
) -> None:
    path = jsonc_path()
    text = _render_jsonc(active, commented, order)
    _validate_after_write(text)
    path.write_text(text, encoding="utf-8")


def merge_active(overrides: dict[str, Any]) -> dict[str, Any]:
    """Merge `overrides` into the active set and persist.

    Existing active values not in `overrides` are kept. Example/commented
    values are preserved. Returns {before_active, after_active, path}.
    """
    state = read_state()
    before = dict(state["active"])
    after = dict(before)
    commented = dict(state["commented"])
    order = list(state["order"])
    for key, raw in overrides.items():
        if not key.startswith("USERPREFS_"):
            raise ValueError(f"key {key!r} must start with USERPREFS_")
        after[key] = _stringify(raw)
        # If the key was commented, uncommenting it means removing from commented set.
        commented.pop(key, None)
        if key not in order:
            order.append(key)
    write_state(after, commented, order)
    return {"before_active": before, "after_active": after, "path": str(jsonc_path())}


def _stringify(value: Any) -> str:
    """Convert a Python value to the string form userPrefs.jsonc expects.

    bool → "true" / "false"; int/float → str(); anything else → str(value).
    Callers passing brace-init strings (`"{ 0x01, 0x02, ... }"`) must format
    them themselves — this function doesn't try to synthesize them.
    """
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return str(value)
    return str(value)


def reset() -> dict[str, Any]:
    """Restore userPrefs.jsonc from the MCP backup if present.

    Returns {restored: bool, path, backup_path}.
    """
    path = jsonc_path()
    backup = path.with_suffix(path.suffix + BACKUP_SUFFIX)
    if backup.is_file():
        shutil.copy2(backup, path)
        backup.unlink()
        return {"restored": True, "path": str(path), "backup_path": str(backup)}
    return {"restored": False, "path": str(path), "backup_path": str(backup)}


# ---------- Transient override (for build/flash) --------------------------


# ---------- Pre-baked profiles --------------------------------------------


def _psk_from_bytes(data: bytes) -> str:
    """Format 32 bytes as a C-style brace-init list for USERPREFS_CHANNEL_*_PSK.

    Matches the exact format used in userPrefs.jsonc:
        { 0x38, 0x4b, 0xbc, ... }
    """
    if len(data) != 32:
        raise ValueError(f"PSK must be exactly 32 bytes, got {len(data)}")
    return "{ " + ", ".join(f"0x{b:02x}" for b in data) + " }"


def generate_psk(seed: str | None = None) -> str:
    """Generate a 32-byte PSK as a brace-init string.

    If `seed` is provided, the PSK is deterministic (derived via SHA-256 of
    the seed); otherwise it's cryptographically random. Use a seed for
    automated testing so every device in a test run shares the same key.
    """
    if seed is None:
        import secrets

        raw = secrets.token_bytes(32)
    else:
        import hashlib

        raw = hashlib.sha256(seed.encode("utf-8")).digest()
    return _psk_from_bytes(raw)


# Meshtastic region enum name → short description (for the manifest tool).
# Not exhaustive; these are the regions a US-based test lab is likely to pick.
KNOWN_REGIONS = {
    "US": "meshtastic_Config_LoRaConfig_RegionCode_US",
    "EU_433": "meshtastic_Config_LoRaConfig_RegionCode_EU_433",
    "EU_868": "meshtastic_Config_LoRaConfig_RegionCode_EU_868",
    "CN": "meshtastic_Config_LoRaConfig_RegionCode_CN",
    "JP": "meshtastic_Config_LoRaConfig_RegionCode_JP",
    "ANZ": "meshtastic_Config_LoRaConfig_RegionCode_ANZ",
    "KR": "meshtastic_Config_LoRaConfig_RegionCode_KR",
    "TW": "meshtastic_Config_LoRaConfig_RegionCode_TW",
    "RU": "meshtastic_Config_LoRaConfig_RegionCode_RU",
    "IN": "meshtastic_Config_LoRaConfig_RegionCode_IN",
    "NZ_865": "meshtastic_Config_LoRaConfig_RegionCode_NZ_865",
    "TH": "meshtastic_Config_LoRaConfig_RegionCode_TH",
    "UA_433": "meshtastic_Config_LoRaConfig_RegionCode_UA_433",
    "UA_868": "meshtastic_Config_LoRaConfig_RegionCode_UA_868",
    "MY_433": "meshtastic_Config_LoRaConfig_RegionCode_MY_433",
    "MY_919": "meshtastic_Config_LoRaConfig_RegionCode_MY_919",
    "SG_923": "meshtastic_Config_LoRaConfig_RegionCode_SG_923",
    "LORA_24": "meshtastic_Config_LoRaConfig_RegionCode_LORA_24",
}

KNOWN_MODEM_PRESETS = {
    "LONG_FAST": "meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST",
    "LONG_SLOW": "meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW",
    "LONG_MODERATE": "meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE",
    "VERY_LONG_SLOW": "meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW",
    "MEDIUM_SLOW": "meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW",
    "MEDIUM_FAST": "meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST",
    "SHORT_SLOW": "meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW",
    "SHORT_FAST": "meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST",
    "SHORT_TURBO": "meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO",
}


def build_testing_profile(
    psk_seed: str | None = None,
    channel_name: str = "McpTest",
    channel_num: int = 88,
    region: str = "US",
    modem_preset: str = "LONG_FAST",
    short_name: str | None = None,
    long_name: str | None = None,
    disable_mqtt: bool = True,
    disable_position: bool = False,
    enable_ui_log: bool = False,
) -> dict[str, Any]:
    """Build a USERPREFS dict for an isolated test-mesh device.

    Defaults: US region, LONG_FAST modem, channel slot 88 (well away from the
    default `hash("LongFast") % numChannels` slot that production devices use),
    and a private PSK. Devices baked with the same `psk_seed` land on the same
    isolated mesh.

    See `src/mesh/RadioInterface.cpp:849` for the slot-selection math:
    `slot = (channel_num ? channel_num - 1 : hash(name)) % numChannels`.
    Setting `channel_num` explicitly (non-zero) forces a deterministic slot.

    Args:
        psk_seed: seed for deterministic PSK generation. `None` = random (fine
            for one-off bakes, useless for multi-device test clusters).
        channel_name: primary channel name. Must differ from defaults
            ("LongFast", "MediumFast", etc.) so production devices don't
            accidentally match after the PSK check.
        channel_num: 1-indexed LoRa slot (1..numChannels). 88 is mid-upper US
            band. Set to 0 to fall back to name-hash (not recommended for
            isolation).
        region: short code from `KNOWN_REGIONS`.
        modem_preset: short code from `KNOWN_MODEM_PRESETS`.
        short_name: optional owner short-name stamp (≤4 chars). None = unset.
        long_name: optional owner long-name stamp. None = unset.
        disable_mqtt: if True (default), disables the MQTT module and the
            uplink/downlink bridge on the primary channel — so private test
            traffic never leaks to a public broker.
        disable_position: if True, disables GPS + position broadcasts — useful
            when test devices sit on a bench without antennas.
        enable_ui_log: if True, stamps `USERPREFS_UI_TEST_LOG=true` so the
            firmware emits one `Screen: frame N/M name=... reason=...` log
            line per frame transition. Test-only; off by default because the
            log is chatty (multiple times per second during UI interaction).

    """
    if region not in KNOWN_REGIONS:
        raise ValueError(
            f"Unknown region {region!r}. Known: {sorted(KNOWN_REGIONS.keys())}"
        )
    if modem_preset not in KNOWN_MODEM_PRESETS:
        raise ValueError(
            f"Unknown modem_preset {modem_preset!r}. Known: {sorted(KNOWN_MODEM_PRESETS.keys())}"
        )
    if not (0 <= channel_num <= 255):
        raise ValueError(f"channel_num must be 0..255, got {channel_num}")
    if len(channel_name) > 11:
        raise ValueError(
            f"channel_name {channel_name!r} exceeds Meshtastic's 11-char max"
        )
    if short_name is not None and len(short_name) > 4:
        raise ValueError(f"short_name must be ≤4 chars, got {len(short_name)}")

    psk = generate_psk(seed=psk_seed)

    prefs: dict[str, Any] = {
        # --- LoRa ---
        "USERPREFS_CONFIG_LORA_REGION": KNOWN_REGIONS[region],
        "USERPREFS_LORACONFIG_MODEM_PRESET": KNOWN_MODEM_PRESETS[modem_preset],
        "USERPREFS_LORACONFIG_CHANNEL_NUM": channel_num,
        # --- Primary channel (isolated from public default) ---
        "USERPREFS_CHANNELS_TO_WRITE": 1,
        "USERPREFS_CHANNEL_0_NAME": channel_name,
        "USERPREFS_CHANNEL_0_PSK": psk,
        "USERPREFS_CHANNEL_0_PRECISION": 14,
    }
    if disable_mqtt:
        prefs.update(
            {
                "USERPREFS_CONFIG_LORA_IGNORE_MQTT": True,
                "USERPREFS_MQTT_ENABLED": 0,
                "USERPREFS_CHANNEL_0_UPLINK_ENABLED": False,
                "USERPREFS_CHANNEL_0_DOWNLINK_ENABLED": False,
            }
        )
    if disable_position:
        prefs.update(
            {
                "USERPREFS_CONFIG_GPS_MODE": "meshtastic_Config_PositionConfig_GpsMode_DISABLED",
                "USERPREFS_CONFIG_SMART_POSITION_ENABLED": False,
            }
        )
    if long_name is not None:
        prefs["USERPREFS_CONFIG_OWNER_LONG_NAME"] = long_name
    if short_name is not None:
        prefs["USERPREFS_CONFIG_OWNER_SHORT_NAME"] = short_name
    if enable_ui_log:
        # Consumed by `#ifdef USERPREFS_UI_TEST_LOG` in src/graphics/Screen.cpp.
        prefs["USERPREFS_UI_TEST_LOG"] = True

    return prefs


@contextmanager
def temporary_overrides(overrides: dict[str, Any] | None) -> Iterator[dict[str, str]]:
    """Apply `overrides` to userPrefs.jsonc for the duration of the context.

    Yields a dict of the *effective* active values (original active merged
    with overrides). Always restores the original file on exit, even on
    exception. If `overrides` is None or empty, this is a no-op.

    The restore writes the original file content byte-for-byte, so there's no
    round-trip ambiguity even if the file had unusual whitespace.
    """
    if not overrides:
        state = read_state()
        yield dict(state["active"])
        return

    path = jsonc_path()
    if not path.is_file():
        raise FileNotFoundError(f"userPrefs.jsonc not found at {path}")

    original_bytes = path.read_bytes()
    original_stat = path.stat()

    # Merge and write
    state = _parse_jsonc_state(original_bytes.decode("utf-8"))
    effective = dict(state["active"])
    commented = dict(state["commented"])
    order = list(state["order"])
    for key, raw in overrides.items():
        if not key.startswith("USERPREFS_"):
            raise ValueError(f"key {key!r} must start with USERPREFS_")
        effective[key] = _stringify(raw)
        commented.pop(key, None)
        if key not in order:
            order.append(key)

    rendered = _render_jsonc(effective, commented, order)
    _validate_after_write(rendered)
    path.write_text(rendered, encoding="utf-8")
    # pio watches file mtimes to invalidate build cache; force the modification
    # time to now so a pre-existing `.pio/build/<env>/` cache is discarded.
    now = time.time()
    import os

    os.utime(path, (now, now))

    try:
        yield effective
    finally:
        path.write_bytes(original_bytes)
        os.utime(path, (original_stat.st_atime, original_stat.st_mtime))
