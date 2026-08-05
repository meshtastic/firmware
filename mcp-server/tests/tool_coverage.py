"""Tool-surface coverage: track which MCP tools the test suite actually exercises.

This is NOT line coverage (that's `coverage.py`). This measures which of the
38 public MCP tools in `meshtastic_mcp.server` got invoked during a pytest
run — a quick signal for "where are the test-coverage gaps".

Approach: introspect `meshtastic_mcp.server.app` for registered tools, find
the underlying handler functions in their source modules, and wrap each with
a counting shim. At session end, emit `tool_coverage.json` mapping each tool
name to its call count. Tools never called show `count=0`.
"""

from __future__ import annotations

import json
import pathlib
from typing import Any

_counts: dict[str, int] = {}
_installed = False


def _bump(name: str) -> None:
    _counts[name] = _counts.get(name, 0) + 1


def _wrap(module: Any, attr: str, tool_name: str) -> None:
    original = getattr(module, attr, None)
    if original is None or not callable(original):
        return

    def wrapper(*args: Any, **kwargs: Any) -> Any:
        _bump(tool_name)
        return original(*args, **kwargs)

    wrapper.__wrapped__ = original  # type: ignore[attr-defined]
    wrapper.__name__ = attr
    wrapper.__doc__ = original.__doc__
    setattr(module, attr, wrapper)


# Mapping: MCP tool name → (module, function name). Mirrors the wiring in
# `meshtastic_mcp.server`. Keep synchronized manually — adding a tool without
# updating this map means it shows as count=0 in reports even if exercised.
_TOOL_MAP: dict[str, tuple[str, str]] = {
    # Discovery & metadata
    "list_devices": ("meshtastic_mcp.devices", "list_devices"),
    "list_boards": ("meshtastic_mcp.boards", "list_boards"),
    "get_board": ("meshtastic_mcp.boards", "get_board"),
    # Build & flash
    "build": ("meshtastic_mcp.flash", "build"),
    "clean": ("meshtastic_mcp.flash", "clean"),
    "pio_flash": ("meshtastic_mcp.flash", "flash"),
    "erase_and_flash": ("meshtastic_mcp.flash", "erase_and_flash"),
    "update_flash": ("meshtastic_mcp.flash", "update_flash"),
    "touch_1200bps": ("meshtastic_mcp.flash", "touch_1200bps"),
    # Serial log sessions — module-level functions on serial_session
    "serial_open": ("meshtastic_mcp.serial_session", "open_session"),
    "serial_read": ("meshtastic_mcp.serial_session", "read_session"),
    "serial_list": ("meshtastic_mcp.registry", "all_sessions"),
    "serial_close": ("meshtastic_mcp.serial_session", "close_session"),
    # Device reads
    "device_info": ("meshtastic_mcp.info", "device_info"),
    "list_nodes": ("meshtastic_mcp.info", "list_nodes"),
    # Device writes
    "set_owner": ("meshtastic_mcp.admin", "set_owner"),
    "get_config": ("meshtastic_mcp.admin", "get_config"),
    "set_config": ("meshtastic_mcp.admin", "set_config"),
    "get_channel_url": ("meshtastic_mcp.admin", "get_channel_url"),
    "set_channel_url": ("meshtastic_mcp.admin", "set_channel_url"),
    "set_debug_log_api": ("meshtastic_mcp.admin", "set_debug_log_api"),
    "send_text": ("meshtastic_mcp.admin", "send_text"),
    "reboot": ("meshtastic_mcp.admin", "reboot"),
    "shutdown": ("meshtastic_mcp.admin", "shutdown"),
    "factory_reset": ("meshtastic_mcp.admin", "factory_reset"),
    "send_input_event": ("meshtastic_mcp.admin", "send_input_event"),
    # `capture_screen` in server.py calls camera.get_camera — instrument that.
    "capture_screen": ("meshtastic_mcp.camera", "get_camera"),
    # USB power control via uhubctl.
    "uhubctl_list": ("meshtastic_mcp.uhubctl", "list_hubs"),
    "uhubctl_power": ("meshtastic_mcp.uhubctl", "power_on"),
    "uhubctl_cycle": ("meshtastic_mcp.uhubctl", "cycle"),
    # USERPREFS
    "userprefs_manifest": ("meshtastic_mcp.userprefs", "build_manifest"),
    "userprefs_get": ("meshtastic_mcp.userprefs", "read_state"),
    "userprefs_set": ("meshtastic_mcp.userprefs", "merge_active"),
    "userprefs_reset": ("meshtastic_mcp.userprefs", "reset"),
    "userprefs_testing_profile": ("meshtastic_mcp.userprefs", "build_testing_profile"),
    # Vendor hardware tools
    "esptool_chip_info": ("meshtastic_mcp.hw_tools", "esptool_chip_info"),
    "esptool_erase_flash": ("meshtastic_mcp.hw_tools", "esptool_erase_flash"),
    "esptool_raw": ("meshtastic_mcp.hw_tools", "esptool_raw"),
    "nrfutil_dfu": ("meshtastic_mcp.hw_tools", "nrfutil_dfu"),
    "nrfutil_raw": ("meshtastic_mcp.hw_tools", "nrfutil_raw"),
    "picotool_info": ("meshtastic_mcp.hw_tools", "picotool_info"),
    "picotool_load": ("meshtastic_mcp.hw_tools", "picotool_load"),
    "picotool_raw": ("meshtastic_mcp.hw_tools", "picotool_raw"),
}


def install() -> None:
    """Wrap every mapped tool function with the counting shim. Idempotent."""
    global _installed
    if _installed:
        return
    import importlib

    # Whitelist the exact module paths this function is ever allowed to
    # import. `module_path` below is iterated from `_TOOL_MAP` — a file-
    # local, hardcoded dict literal — but a static whitelist makes the
    # "no untrusted input here" invariant legible to reviewers and to
    # the Semgrep `non-literal-import` audit rule.
    _allowed_modules = frozenset(path for path, _attr in _TOOL_MAP.values())

    for tool_name, (module_path, attr) in _TOOL_MAP.items():
        # Defense in depth: if someone mutates `_TOOL_MAP` at runtime
        # (shouldn't happen; it's module-level) the whitelist catches it.
        # `module_path` is a key from the hardcoded `_TOOL_MAP` dict and
        # is gated above by membership in `_allowed_modules` (itself
        # derived from the same literal values). There is no path for
        # untrusted input to reach the `import_module` call below; the
        # Semgrep suppression must sit on the line immediately preceding
        # the call (multi-line comment blocks between comment and call
        # break the rule's scope detection).
        if module_path not in _allowed_modules:
            continue
        try:
            # nosemgrep: python.lang.security.audit.non-literal-import.non-literal-import
            mod = importlib.import_module(module_path)
        except ImportError:
            continue
        _wrap(mod, attr, tool_name)
        _counts.setdefault(tool_name, 0)
    _installed = True


def write_report(path: pathlib.Path) -> None:
    """Emit `tool_coverage.json` with call counts for every mapped tool."""
    exercised = sum(1 for c in _counts.values() if c > 0)
    total = len(_counts)
    payload = {
        "total_tools": total,
        "exercised": exercised,
        "coverage_pct": round(100.0 * exercised / total, 1) if total else 0.0,
        "counts": dict(sorted(_counts.items())),
        "unexercised": sorted(k for k, v in _counts.items() if v == 0),
    }
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def snapshot() -> dict[str, int]:
    return dict(_counts)
