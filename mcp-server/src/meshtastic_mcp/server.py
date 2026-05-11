"""FastMCP server wiring — 43 tools across 9 categories (adds uhubctl power control).

Each tool handler is a thin delegation to a named module (pio.py, admin.py,
etc.). Business logic does not live here.
"""

from __future__ import annotations

import logging
from typing import Any

from mcp.server.fastmcp import FastMCP

from . import (
    admin,
    boards,
    devices,
    flash,
    hw_tools,
    info,
    log_query,
    registry,
    serial_session,
)
from . import userprefs as userprefs_mod
from .recorder import get_recorder

log = logging.getLogger(__name__)

app = FastMCP("meshtastic-mcp")


def _start_recorder() -> None:
    # Persistent device-log capture. Starts on first import — pubsub fan-out
    # is process-global, so subscribing here captures every active interface
    # (whether opened by an MCP tool, a pytest fixture, or a serial_session).
    # Files land in mcp-server/.mtlog/ (gitignored). See recorder/recorder.py
    # for the full design. Recorder startup is best-effort: an unwritable
    # log dir or pubsub mismatch should not take the MCP server down.
    try:
        get_recorder().start()
    except Exception as exc:
        log.warning("Failed to start persistent recorder: %s", exc)


_start_recorder()


# ---------- Discovery & metadata ------------------------------------------


@app.tool()
def list_devices(include_unknown: bool = False) -> list[dict[str, Any]]:
    """List USB/serial ports, flagging those likely to be Meshtastic devices.

    With include_unknown=True, returns every serial port the OS knows about
    (useful for debugging when a device isn't detected). Otherwise returns
    only likely-Meshtastic candidates.
    """
    return devices.list_devices(include_unknown=include_unknown)


@app.tool()
def list_boards(
    architecture: str | None = None,
    actively_supported_only: bool = False,
    query: str | None = None,
    board_level: str | None = None,
) -> list[dict[str, Any]]:
    """Enumerate PlatformIO envs (boards) with Meshtastic metadata.

    architecture: filter to one arch ("esp32", "esp32s3", "nrf52840", "rp2040", "stm32", "native").
    actively_supported_only: filter to boards marked custom_meshtastic_actively_supported=true.
    query: substring match on display_name, env name, or hw_model_slug (case-insensitive).
    board_level: "release" (default-track release boards), "pr" (PR CI), or "extra" (opt-in extras).
    """
    return boards.list_boards(
        architecture=architecture,
        actively_supported_only=actively_supported_only,
        query=query,
        board_level=board_level,
    )


@app.tool()
def get_board(env: str) -> dict[str, Any]:
    """Full metadata for one PlatformIO env, including raw pio config fields."""
    return boards.get_board(env)


# ---------- Build & flash -------------------------------------------------


@app.tool()
def build(
    env: str,
    with_manifest: bool = True,
    userprefs: dict[str, Any] | None = None,
    build_flags: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Build firmware for one env via `pio run -e <env>`.

    Returns exit_code, duration, artifact paths under .pio/build/<env>/, and
    tails of stdout/stderr (last 200 lines each). with_manifest=True adds the
    mtjson target which produces an .mt.json manifest alongside the firmware.

    `userprefs` (optional): dict of `USERPREFS_<KEY>: value` baked into this
    build via userPrefs.jsonc injection. The file is restored after the build
    completes. Use `userprefs_manifest` to discover available keys. Use
    `userprefs_set` for persistent changes.

    `build_flags` (optional): dict of `-D<NAME>=<VALUE>` macros for this build
    only, injected via `PLATFORMIO_BUILD_FLAGS`. Common pattern:
    `build_flags={"DEBUG_HEAP": 1}` enables per-thread leak detection + a
    `[heap N]` prefix on every log line. The recorder picks the prefix up
    automatically and synthesizes a high-resolution heap timeline that
    `telemetry_timeline(field="free_heap")` can read alongside the normal
    ~60 s LocalStats packets. Pair with `/leakhunt` for classification.
    """
    return flash.build(
        env,
        with_manifest=with_manifest,
        userprefs_overrides=userprefs,
        build_flags=build_flags,
    )


@app.tool()
def clean(env: str) -> dict[str, Any]:
    """Clean one env's build output via `pio run -e <env> -t clean`.

    Useful when switching branches or debugging a stale-cache build failure.
    """
    return flash.clean(env)


@app.tool()
def pio_flash(
    env: str,
    port: str,
    confirm: bool = False,
    userprefs: dict[str, Any] | None = None,
    build_flags: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Flash firmware via `pio run -e <env> -t upload --upload-port <port>`.

    Works for any architecture (ESP32/nRF52/RP2040/STM32). Requires confirm=True.
    For first-time flashing a blank ESP32 board (erase + bootloader + app + fs),
    prefer `erase_and_flash`. For ESP32 OTA updates, prefer `update_flash`.

    `userprefs` (optional): dict of `USERPREFS_<KEY>: value` baked into this
    build via userPrefs.jsonc injection; restored after upload.

    `build_flags` (optional): dict of `-D<NAME>=<VALUE>` macros for the
    rebuild-before-upload, e.g. `{"DEBUG_HEAP": 1}`. Required for the flags
    to actually land in the uploaded firmware — without it, the implicit
    rebuild relinks without the env var and silently drops them.
    """
    return flash.flash(
        env,
        port,
        confirm=confirm,
        userprefs_overrides=userprefs,
        build_flags=build_flags,
    )


@app.tool()
def erase_and_flash(
    env: str,
    port: str,
    confirm: bool = False,
    skip_build: bool = False,
    userprefs: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """ESP32-only: full erase + factory flash via bin/device-install.sh.

    Wipes the entire flash and writes bootloader, app, OTA, and LittleFS
    partitions from the factory.bin. Requires confirm=True. Runs `build` first
    if no factory.bin is present (set skip_build=True to require a prior build).

    `userprefs` (optional): dict of `USERPREFS_<KEY>: value` baked into the
    factory.bin via userPrefs.jsonc injection. When provided, forces a rebuild
    (skip_build=True is incompatible). File is restored after upload.
    """
    return flash.erase_and_flash(
        env, port, confirm=confirm, skip_build=skip_build, userprefs_overrides=userprefs
    )


@app.tool()
def update_flash(
    env: str,
    port: str,
    confirm: bool = False,
    skip_build: bool = False,
    userprefs: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """ESP32-only: OTA app-partition update via bin/device-update.sh.

    Updates only the application partition, preserving device config and node
    database. Faster than erase_and_flash but won't recover a broken bootloader.
    Requires confirm=True. Builds first if needed.

    `userprefs` (optional): dict of `USERPREFS_<KEY>: value` baked into the
    firmware.bin via userPrefs.jsonc injection. When provided, forces a rebuild.
    """
    return flash.update_flash(
        env, port, confirm=confirm, skip_build=skip_build, userprefs_overrides=userprefs
    )


# ---------- USERPREFS discovery & persistence -----------------------------


@app.tool()
def userprefs_manifest() -> dict[str, Any]:
    """Full manifest of USERPREFS_* keys the firmware knows about.

    Combines `userPrefs.jsonc` (active + commented examples) with a scan of
    `src/**` for `USERPREFS_<KEY>` references — so every key the firmware
    actually consumes shows up, even if undocumented in the jsonc.

    Each entry has: key, active (is it uncommented), value (current), example
    (jsonc commented default), declared_in_jsonc, consumed_by (list of src
    files), inferred_type (brace|number|bool|enum|string|unknown).

    `inferred_type` mirrors how platformio-custom.py wraps values at build
    time: `brace` = byte array `{ 0x01, ... }`, `number` = decimal, `bool` =
    true/false, `enum` = `meshtastic_*` constant, `string` = wrapped in quotes
    via StringifyMacro.
    """
    return userprefs_mod.build_manifest()


@app.tool()
def userprefs_get() -> dict[str, Any]:
    """Return the current userPrefs.jsonc state.

    `active` is the dict of uncommented `USERPREFS_*` → value that will be
    baked into the next build. `commented` is the dict of commented example
    defaults (shown for reference).
    """
    state = userprefs_mod.read_state()
    # Drop `order` (internal for round-trip rendering) from the public payload.
    return {
        "path": state["path"],
        "active": state["active"],
        "commented": state["commented"],
    }


@app.tool()
def userprefs_set(prefs: dict[str, Any]) -> dict[str, Any]:
    """Merge `prefs` into userPrefs.jsonc persistently (uncommenting keys).

    Existing active values not in `prefs` are kept. To remove a key from the
    active set, call `userprefs_reset` (restores the MCP backup if present)
    or edit the jsonc manually. Values are stringified the way
    platformio-custom.py expects (bool → "true"/"false", int → "42", etc.).
    """
    return userprefs_mod.merge_active(prefs)


@app.tool()
def userprefs_reset() -> dict[str, Any]:
    """Restore userPrefs.jsonc from the most recent MCP backup (if any).

    The backup is only created by the legacy `userprefs_set` workflow (not
    currently written automatically). Returns `{restored: bool, ...}` — false
    when no backup is present, in which case the caller should edit the
    jsonc directly.
    """
    return userprefs_mod.reset()


@app.tool()
def userprefs_testing_profile(
    psk_seed: str | None = None,
    channel_name: str = "McpTest",
    channel_num: int = 88,
    region: str = "US",
    modem_preset: str = "LONG_FAST",
    short_name: str | None = None,
    long_name: str | None = None,
    disable_mqtt: bool = True,
    disable_position: bool = False,
) -> dict[str, Any]:
    """Generate a USERPREFS dict for provisioning an isolated test-mesh device.

    Baking this into firmware produces devices that:
      - Run on a deterministic non-default LoRa slot (default 88 on US LONG_FAST,
        well off the `hash("LongFast")` slot a stock production device uses)
      - Join a private channel with a name and PSK that differ from public
        defaults — so no accidental mesh-with-production-devices
      - Have MQTT disabled (no uplink/downlink bridge), so test traffic never
        leaks to a public broker
      - Optionally disable GPS for bench-test conditions

    For a multi-device test cluster, pass the same `psk_seed` to every call so
    every device shares the same PSK and lands on the same isolated mesh.

    Returned dict is ready to pass straight to `build`, `pio_flash`,
    `erase_and_flash`, or `update_flash` via their `userprefs` parameter.

    Example:
        profile = userprefs_testing_profile(psk_seed="ci-run-2026-04-16")
        erase_and_flash(env="tbeam", port="/dev/cu.usbmodem...", confirm=True,
                        userprefs=profile)

    Args:
        psk_seed: seed for deterministic 32-byte PSK via SHA-256. None = random
            (fine one-off, useless for multi-device clusters).
        channel_name: primary channel name (≤11 chars). Default "McpTest".
        channel_num: 1-indexed LoRa slot (0 = fall back to name-hash). Default
            88 — mid-upper US band, unlikely to collide with production slots.
        region: short code — one of US, EU_433, EU_868, CN, JP, ANZ, KR, TW,
            RU, IN, NZ_865, TH, UA_433, UA_868, MY_433, MY_919, SG_923, LORA_24.
        modem_preset: one of LONG_FAST, LONG_SLOW, LONG_MODERATE, VERY_LONG_SLOW,
            MEDIUM_SLOW, MEDIUM_FAST, SHORT_SLOW, SHORT_FAST, SHORT_TURBO.
        short_name: optional owner short name (≤4 chars) stamped into the build.
        long_name: optional owner long name stamped into the build.
        disable_mqtt: disable MQTT module + uplink/downlink (default True).
        disable_position: disable GPS + smart-position broadcasts (default False).

    """
    return userprefs_mod.build_testing_profile(
        psk_seed=psk_seed,
        channel_name=channel_name,
        channel_num=channel_num,
        region=region,
        modem_preset=modem_preset,
        short_name=short_name,
        long_name=long_name,
        disable_mqtt=disable_mqtt,
        disable_position=disable_position,
    )


@app.tool()
def touch_1200bps(port: str, settle_ms: int = 250) -> dict[str, Any]:
    """Open `port` at 1200 baud and immediately close, triggering USB CDC
    bootloader entry on nRF52840, ESP32-S3 (native USB), RP2040, etc.

    After the touch, polls serial devices for up to 3 seconds and reports any
    new port that appeared (the bootloader often enumerates as a different
    device). Not destructive — this is just a reset signal.
    """
    return flash.touch_1200bps(port, settle_ms=settle_ms)


# ---------- Serial log sessions -------------------------------------------


@app.tool()
def serial_open(
    port: str,
    baud: int = 115200,
    env: str | None = None,
    filters: list[str] | None = None,
) -> dict[str, Any]:
    """Open a `pio device monitor` session reading from `port`.

    If `env` is set, pio picks up monitor_speed and monitor_filters from
    platformio.ini — recommended for firmware debugging since it enables
    esp32_exception_decoder / esp32_c3_exception_decoder for ESP32 envs.

    Without `env`, uses the supplied baud and filters (default ["direct"]).
    Common filters: direct, time, hexlify, esp32_exception_decoder,
    esp32_c3_exception_decoder, log2file.

    Returns a session_id for use with serial_read / serial_close, plus the
    resolved baud and filters so callers can confirm what pio selected.
    """
    session = serial_session.open_session(
        port=port, baud=baud, env=env, filters=filters
    )
    registry.register_session(session)
    return {
        "session_id": session.id,
        "resolved_baud": session.baud,
        "resolved_filters": session.filters,
        "env": session.env,
    }


@app.tool()
def serial_read(
    session_id: str,
    max_lines: int = 200,
    since_cursor: int | None = None,
) -> dict[str, Any]:
    """Read buffered lines from a serial monitor session.

    Default: returns everything since your last call to serial_read (uses an
    advancing cursor). Pass `since_cursor=N` to re-read from a specific point,
    or `since_cursor=0` to read from the start of the in-memory buffer.

    Returns `dropped` = count of lines that aged out of the 10k-line ring
    buffer between reads — so a value > 0 means you missed data.
    """
    session = registry.get_session(session_id)
    return serial_session.read_session(
        session, max_lines=max_lines, since_cursor=since_cursor
    )


@app.tool()
def serial_list() -> list[dict[str, Any]]:
    """List all active serial monitor sessions."""
    return [serial_session.session_summary(s) for s in registry.all_sessions()]


@app.tool()
def serial_close(session_id: str) -> dict[str, Any]:
    """Terminate a serial monitor session and free its port."""
    session = registry.remove_session(session_id)
    if session is None:
        return {"ok": False, "reason": f"Unknown session_id {session_id!r}"}
    serial_session.close_session(session)
    return {"ok": True}


# ---------- Device interaction: reads -------------------------------------


@app.tool()
def device_info(port: str | None = None, timeout_s: float = 8.0) -> dict[str, Any]:
    """Connect via meshtastic.SerialInterface and return a summary of the node.

    If `port` is omitted and exactly one likely-Meshtastic device is connected,
    it's auto-selected; otherwise the tool errors with the candidate list.
    """
    return info.device_info(port=port, timeout_s=timeout_s)


@app.tool()
def list_nodes(port: str | None = None, timeout_s: float = 8.0) -> list[dict[str, Any]]:
    """Return the device's current node database (local node + all known peers)."""
    return info.list_nodes(port=port, timeout_s=timeout_s)


# ---------- Device interaction: writes ------------------------------------


@app.tool()
def set_owner(
    long_name: str, short_name: str | None = None, port: str | None = None
) -> dict[str, Any]:
    """Set the device's owner long name and (optional) short name (≤4 chars)."""
    return admin.set_owner(long_name=long_name, short_name=short_name, port=port)


@app.tool()
def get_config(section: str | None = None, port: str | None = None) -> dict[str, Any]:
    """Read one or all config sections.

    `section` may be any LocalConfig section (device, position, power, network,
    display, lora, bluetooth, security) or LocalModuleConfig section (mqtt,
    serial, telemetry, external_notification, canned_message, range_test,
    store_forward, neighbor_info, ambient_lighting, detection_sensor,
    paxcounter, audio, remote_hardware, statusmessage, traffic_management).
    Omit or pass "all" for every section.
    """
    return admin.get_config(section=section, port=port)


@app.tool()
def set_config(path: str, value: Any, port: str | None = None) -> dict[str, Any]:
    """Set one config field via dot-path and write it to the device.

    Examples: "lora.region"="US", "lora.modem_preset"="LONG_FAST",
    "device.role"="ROUTER", "mqtt.enabled"=True, "mqtt.address"="host".
    Enum fields accept their name (case-insensitive) or int.
    """
    return admin.set_config(path=path, value=value, port=port)


@app.tool()
def get_channel_url(
    include_all: bool = False, port: str | None = None
) -> dict[str, Any]:
    """Get the shareable channel URL (QR-code content).

    include_all=True returns the admin URL including all secondary channels;
    False returns only the primary channel (what users typically share).
    """
    return admin.get_channel_url(include_all=include_all, port=port)


@app.tool()
def set_channel_url(url: str, port: str | None = None) -> dict[str, Any]:
    """Import channels from a Meshtastic channel URL."""
    return admin.set_channel_url(url=url, port=port)


@app.tool()
def set_debug_log_api(enabled: bool, port: str | None = None) -> dict[str, Any]:
    """Toggle security.debug_log_api_enabled on the local node.

    When true, firmware streams log lines as protobuf `LogRecord` messages
    over the StreamAPI (topic `meshtastic.log.line` in meshtastic-python)
    instead of raw text. Lets diagnostic clients capture firmware-side logs
    through the SAME SerialInterface used for admin/info calls — no
    separate `pio device monitor` session needed, no exclusive-port-lock
    conflict. Persists across reboot via NVS; wiped by factory_reset
    unless re-applied.

    The earlier emitLogRecord race (shared tx buffer) is fixed at the
    firmware level — the log path has a dedicated scratch + txBuf and
    both emission paths serialize via a mutex. Safe to leave on under
    traffic.
    """
    return admin.set_debug_log_api(enabled=enabled, port=port)


@app.tool()
def send_text(
    text: str,
    to: str | int | None = None,
    channel_index: int = 0,
    want_ack: bool = False,
    port: str | None = None,
) -> dict[str, Any]:
    """Send a text message over the mesh.

    `to` defaults to broadcast ("^all"). Pass a node ID (hex string like
    "!abcdef01") or node number (int) to direct-message a specific node.
    channel_index picks which configured channel to send on.
    """
    return admin.send_text(
        text=text, to=to, channel_index=channel_index, want_ack=want_ack, port=port
    )


@app.tool()
def reboot(
    port: str | None = None, confirm: bool = False, seconds: int = 10
) -> dict[str, Any]:
    """Reboot the connected node in `seconds` seconds. Requires confirm=True."""
    return admin.reboot(port=port, confirm=confirm, seconds=seconds)


@app.tool()
def shutdown(
    port: str | None = None, confirm: bool = False, seconds: int = 10
) -> dict[str, Any]:
    """Shut down the connected node in `seconds` seconds. Requires confirm=True."""
    return admin.shutdown(port=port, confirm=confirm, seconds=seconds)


@app.tool()
def factory_reset(
    port: str | None = None, confirm: bool = False, full: bool = False
) -> dict[str, Any]:
    """Factory-reset the connected node. Requires confirm=True.

    `full=True` also wipes device identity/keys (not just config).
    """
    return admin.factory_reset(port=port, confirm=confirm, full=full)


@app.tool()
def send_input_event(
    event_code: int | str,
    kb_char: int = 0,
    touch_x: int = 0,
    touch_y: int = 0,
    port: str | None = None,
) -> dict[str, Any]:
    """Inject an InputBroker event (button / key / gesture) into the device UI.

    Drives the same code path as a physical button press. Accepts a numeric
    event code (0..255) or a name like `"RIGHT"`, `"SELECT"`, `"FN_F1"`.

    Common codes: SELECT=10, UP=17, DOWN=18, LEFT=19, RIGHT=20, CANCEL=24,
    BACK=27, FN_F1..F5=241..245.
    """
    return admin.send_input_event(
        event_code=event_code,
        kb_char=kb_char,
        touch_x=touch_x,
        touch_y=touch_y,
        port=port,
    )


@app.tool()
def capture_screen(role: str | None = None, ocr: bool = True) -> dict[str, Any]:
    """Grab a frame from the USB webcam pointed at the device screen.

    Returns PNG bytes (base64), optional OCR text, and backend metadata.
    Requires the `[ui]` extras (opencv-python-headless) and a camera
    configured via `MESHTASTIC_UI_CAMERA_DEVICE[_<ROLE>]`. Falls back to a
    1×1 black PNG from the null backend when no camera is configured.
    """
    import base64

    from . import camera as camera_mod

    cam = camera_mod.get_camera(role)
    try:
        png = cam.capture()
    finally:
        cam.close()

    result: dict[str, Any] = {
        "backend": cam.name,
        "bytes": len(png),
        "image_base64": base64.b64encode(png).decode("ascii"),
    }
    if ocr:
        from . import ocr as ocr_mod

        result["ocr_backend"] = ocr_mod.backend_name()
        result["ocr_text"] = ocr_mod.ocr_text(png)
    return result


# ---------- USB power control (uhubctl) -----------------------------------


@app.tool()
def uhubctl_list() -> list[dict[str, Any]]:
    """List every USB hub + per-port device attachment as seen by `uhubctl`.

    Read-only — no confirm required. Each hub entry includes its location
    (`1-1.3`), descriptor, whether it supports Per-Port Power Switching,
    and a list of populated ports with VID:PID of attached devices.
    Useful for pre-flight checks before a destructive power-cycle call.
    """
    from . import uhubctl as uhubctl_mod

    return uhubctl_mod.list_hubs()


@app.tool()
def uhubctl_power(
    action: str,
    location: str | None = None,
    port: int | None = None,
    role: str | None = None,
    confirm: bool = False,
) -> dict[str, Any]:
    """Power a USB hub port on or off via `uhubctl -a on|off`.

    Target the port by either (`location`, `port`) — raw uhubctl syntax,
    e.g. `location="1-1.3", port=2` — OR by `role` ("nrf52", "esp32s3").
    Role lookup honors `MESHTASTIC_UHUBCTL_LOCATION_<ROLE>` +
    `_PORT_<ROLE>` env vars first, falls back to VID auto-detection.

    `action="off"` requires `confirm=True` (destructive — the attached
    device will immediately disappear from the OS).
    """
    from . import uhubctl as uhubctl_mod

    action_lower = action.lower()
    if action_lower not in {"on", "off"}:
        raise ValueError(f"action must be 'on' or 'off', got {action!r}")
    if action_lower == "off" and not confirm:
        raise uhubctl_mod.UhubctlError(
            "uhubctl_power action='off' requires confirm=True"
        )
    loc, p = _resolve_uhubctl_target(location, port, role)
    if action_lower == "on":
        return uhubctl_mod.power_on(loc, p)
    return uhubctl_mod.power_off(loc, p)


@app.tool()
def uhubctl_cycle(
    location: str | None = None,
    port: int | None = None,
    role: str | None = None,
    delay_s: int = 2,
    confirm: bool = False,
) -> dict[str, Any]:
    """Power a USB hub port off, wait `delay_s` seconds, then on.

    The typical hard-reset sequence — shorter than off+on as two RPCs
    because uhubctl handles the timing in-process. Target by (location,
    port) or by role (see `uhubctl_power`). Requires `confirm=True`.
    """
    from . import uhubctl as uhubctl_mod

    if not confirm:
        raise uhubctl_mod.UhubctlError("uhubctl_cycle requires confirm=True")
    if delay_s < 0 or delay_s > 60:
        raise ValueError(f"delay_s must be 0..60, got {delay_s}")
    loc, p = _resolve_uhubctl_target(location, port, role)
    return uhubctl_mod.cycle(loc, p, delay_s=delay_s)


def _resolve_uhubctl_target(
    location: str | None, port: int | None, role: str | None
) -> tuple[str, int]:
    """Shared arg-resolution for uhubctl_power + uhubctl_cycle."""
    from . import uhubctl as uhubctl_mod

    if role is not None:
        if location is not None or port is not None:
            raise ValueError("pass either `role` OR (`location` + `port`), not both")
        return uhubctl_mod.resolve_target(role)
    if location is None or port is None:
        raise ValueError("must pass `role` or both `location` and `port`")
    return (location, int(port))


# ---------- Direct hardware tools -----------------------------------------


@app.tool()
def esptool_chip_info(port: str) -> dict[str, Any]:
    """Run `esptool flash_id` and return chip, MAC, crystal, and flash size.

    Read-only — no confirm required. Prefer this over parsing pio upload logs
    when you just want to identify the chip.
    """
    return hw_tools.esptool_chip_info(port)


@app.tool()
def esptool_erase_flash(port: str, confirm: bool = False) -> dict[str, Any]:
    """Full-chip erase via `esptool erase_flash`. Leaves the device unbootable.

    Prefer `erase_and_flash` which also writes firmware. Use this only for
    recovery when a device is in a weird state. Requires confirm=True.
    """
    return hw_tools.esptool_erase_flash(port, confirm=confirm)


@app.tool()
def esptool_raw(
    args: list[str], port: str | None = None, confirm: bool = False
) -> dict[str, Any]:
    """Pass-through to `esptool`. Destructive subcommands (write_flash,
    erase_flash, erase_region, merge_bin) require confirm=True.

    Prefer the high-level `pio_flash` / `erase_and_flash` / `update_flash`
    tools where possible — they know board-specific offsets and protocols.
    """
    return hw_tools.esptool_raw(args, port=port, confirm=confirm)


@app.tool()
def nrfutil_dfu(port: str, package_path: str, confirm: bool = False) -> dict[str, Any]:
    """DFU-flash a .zip package to an nRF52840 via `nrfutil dfu serial`.

    Prefer `pio_flash` for flashing firmware built from this repo — pio handles
    the DFU invocation automatically. Use this tool when flashing a pre-built
    release zip or a custom bootloader. Requires confirm=True.
    """
    return hw_tools.nrfutil_dfu(port, package_path, confirm=confirm)


@app.tool()
def nrfutil_raw(args: list[str], confirm: bool = False) -> dict[str, Any]:
    """Pass-through to `nrfutil`. dfu/settings subcommands require confirm=True."""
    return hw_tools.nrfutil_raw(args, confirm=confirm)


@app.tool()
def picotool_info(port: str | None = None) -> dict[str, Any]:
    """Run `picotool info -a`. Requires the RP2040 to be in BOOTSEL mode
    (hold BOOTSEL button while plugging in, or call `touch_1200bps` if the
    firmware supports 1200bps-reset)."""
    return hw_tools.picotool_info(port=port)


@app.tool()
def picotool_load(uf2_path: str, confirm: bool = False) -> dict[str, Any]:
    """Load a UF2 to a Pico in BOOTSEL mode via `picotool load -x -t uf2`.

    Prefer `pio_flash` for flashing firmware built from this repo.
    Requires confirm=True.
    """
    return hw_tools.picotool_load(uf2_path, confirm=confirm)


@app.tool()
def picotool_raw(args: list[str], confirm: bool = False) -> dict[str, Any]:
    """Pass-through to `picotool`. load/reboot/save/erase require confirm=True."""
    return hw_tools.picotool_raw(args, confirm=confirm)


# ---------- Persistent device-log capture (recorder) ----------------------
#
# The recorder is autouse — it starts at server import and continuously
# writes every meshtastic pubsub event to JSONL files under .mtlog/. These
# tools are query-only over those files, plus a few lifecycle controls.


@app.tool()
def logs_window(
    start: str = "-15m",
    end: str = "now",
    grep: str | None = None,
    level: str | None = None,
    tag: str | None = None,
    port: str | None = None,
    max_lines: int = 200,
) -> dict[str, Any]:
    """Recent firmware log lines from the persistent recorder.

    Filters by time window, regex over the line, level (single or
    pipe-separated set like "WARN|ERROR|CRIT"), thread-name tag, and
    interface port. Returns up to max_lines most-recent matches.

    Time strings: "-15m", "-2h", "-3d", "now", or ISO 8601.

    Note: lines arriving via the LogRecord protobuf path (when
    set_debug_log_api(True) is on) come without level prefix — the
    meshtastic Python lib drops record.level before fan-out. For those,
    `level` filter won't match; use `grep` instead.
    """
    return log_query.logs_window(
        start=start,
        end=end,
        grep=grep,
        level=level,
        tag=tag,
        port=port,
        max_lines=max_lines,
    )


@app.tool()
def telemetry_timeline(
    window: str = "1h",
    variant: str = "local",
    field: str = "free_heap",
    port: str | None = None,
    max_points: int = 200,
) -> dict[str, Any]:
    """Time series of one telemetry field, downsampled to <= max_points.

    `variant` ∈ device, local, environment, power, airQuality, health, host.
    `field` accepts snake_case or camelCase; common aliases (free_heap ↔
    heap_free_bytes) are normalized.

    Returns slope_per_min (linear-regression slope, units/minute) so a
    leak detector can read one number — negative slope on free_heap over
    a long window indicates a real leak.

    LocalStats variant ("local") cadence is ~60 s (whatever the device's
    `device_update_interval` is set to), so a 1 h window gives ~60 raw
    points. Bucket-mean downsampling preserves shape.
    """
    return log_query.telemetry_timeline(
        window=window,
        variant=variant,
        field=field,
        port=port,
        max_points=max_points,
    )


@app.tool()
def packets_window(
    start: str = "-5m",
    end: str = "now",
    portnum: str | None = None,
    from_node: str | None = None,
    to_node: str | None = None,
    max: int = 200,
) -> dict[str, Any]:
    """Recent mesh packets recorded by the recorder.

    Each row is a summary (portnum, from/to, hop_limit, RSSI/SNR, payload
    size + first 64 bytes hex) — full payload bytes are not stored.
    `portnum` accepts a pipe-separated set like "TEXT_MESSAGE_APP|POSITION_APP".
    """
    return log_query.packets_window(
        start=start,
        end=end,
        portnum=portnum,
        from_node=from_node,
        to_node=to_node,
        max=max,
    )


@app.tool()
def events_window(
    start: str = "-1h",
    end: str = "now",
    kind: str | None = None,
    max: int = 200,
) -> dict[str, Any]:
    """Return recorder events: connection lifecycle, node updates, and `mark_event` markers.

    `kind` ∈ recorder_start, recorder_pause, recorder_resume,
    connection_established, connection_lost, node_updated, mark.
    Pipe-separated sets ("connection_lost|connection_established") work.
    """
    return log_query.events_window(start=start, end=end, kind=kind, max=max)


@app.tool()
def mark_event(
    label: str,
    note: str | None = None,
    data: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Drop a named marker into events.jsonl AND logs.jsonl.

    Useful for aligning a timeline around a known stimulus: call before
    and after a stress workload, then query telemetry_timeline /
    logs_window with the markers' timestamps as bounds.

    The marker also lands in logs.jsonl with level=MARK so a single
    grep over logs picks it up.
    """
    return get_recorder().mark_event(label=label, note=note, data=data)


@app.tool()
def recorder_status() -> dict[str, Any]:
    """Return recorder runtime info: running, paused, file sizes, last_ts per stream.

    Use this to sanity-check that capture is working before you trust a
    `logs_window` / `telemetry_timeline` result.
    """
    return get_recorder().status()


@app.tool()
def recorder_pause(reason: str | None = None) -> dict[str, Any]:
    """Pause writes to all four streams. Pubsub subscriptions stay active —
    we just drop events on the floor while paused. Resume with `recorder_resume`.

    Use when capturing a known-good baseline that you don't want to
    pollute with pre-test noise. Default state is recording; this is
    rarely needed.
    """
    get_recorder().pause(reason=reason)
    return {"ok": True, "paused": True, "reason": reason}


@app.tool()
def recorder_resume() -> dict[str, Any]:
    """Resume writes after `recorder_pause`. No-op if already running."""
    get_recorder().resume()
    return {"ok": True, "paused": False}


@app.tool()
def recorder_export(
    start: str,
    end: str,
    dest_dir: str,
    streams: list[str] | None = None,
) -> dict[str, Any]:
    """Bundle a slice of the recorder's streams into `dest_dir`.

    Writes one uncompressed JSONL per requested stream (logs / telemetry /
    packets / events). Useful for: attaching to a bug report, feeding a
    notebook, or backfilling Datadog after the fact.
    """
    return log_query.export(
        start=start,
        end=end,
        dest_dir=dest_dir,
        streams=streams,
    )
