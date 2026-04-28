"""UI-tier fixtures: camera lifecycle, OCR warmup, per-test frame capture,
and a `ui_home_state` autouse guard that resets to the home frame before
every test (prevents state bleed if a prior test exited inside a menu).

The camera + OCR modules live in `meshtastic_mcp/{camera,ocr}.py` (production
code, so the `capture_screen` MCP tool can share them). These fixtures wire
them into pytest + write per-test captures to `tests/ui_captures/…`.
"""

from __future__ import annotations

import re
import shutil
import time
from pathlib import Path
from typing import Any, Iterator

import pytest
from meshtastic_mcp import admin as admin_mod
from meshtastic_mcp import camera as camera_mod
from meshtastic_mcp import ocr as ocr_mod
from meshtastic_mcp.input_events import InputEventCode

from ._screen_log import FrameEvent, get_current_frame, wait_for_frame

# Roles that carry a screen the UI tier can drive. Only esp32s3 (heltec-v3
# SSD1306) qualifies today — nrf52 (rak4631) has no display.
UI_CAPABLE_ROLES = ("esp32s3",)

# Where per-test captures land. One subdirectory per session seed, then per
# sanitized test nodeid — identical pattern to other pytest artifacts.
CAPTURES_ROOT = Path(__file__).resolve().parent.parent / "ui_captures"


def _sanitize_nodeid(nodeid: str) -> str:
    return re.sub(r"[^a-zA-Z0-9_.-]+", "_", nodeid)


# ---------- Role gating ----------------------------------------------------


@pytest.fixture
def ui_capable_role(request: pytest.FixtureRequest, hub_devices: dict[str, Any]) -> str:
    """Resolve the single role the UI tier drives.

    Today that's `esp32s3`. Skips if the hub doesn't have one. A future
    multi-screen hub could pick a role per parametrization.
    """
    for role in UI_CAPABLE_ROLES:
        if role in hub_devices:
            return role
    pytest.skip(
        f"no UI-capable role on hub; need one of {UI_CAPABLE_ROLES} in {sorted(hub_devices)}"
    )


@pytest.fixture
def ui_port(ui_capable_role: str, hub_devices: dict[str, Any]) -> str:
    port = (
        hub_devices[ui_capable_role].get("port")
        if isinstance(hub_devices[ui_capable_role], dict)
        else hub_devices[ui_capable_role]
    )
    if not port:
        pytest.skip(f"{ui_capable_role!r} has no usable port")
    return port


# ---------- Camera + OCR session fixtures ---------------------------------


@pytest.fixture(scope="session")
def camera(ui_capable_role_session: str | None) -> Iterator[camera_mod.CameraBackend]:
    """Session-scoped camera backend. Closed at teardown.

    Backend + device selected by env vars (see `meshtastic_mcp.camera`).
    Falls through to `NullBackend` when no camera is configured, so the
    tests run end-to-end on machines without hardware; they just won't
    have useful images.
    """
    role = ui_capable_role_session or "esp32s3"
    cam = camera_mod.get_camera(role)
    try:
        yield cam
    finally:
        cam.close()


@pytest.fixture(scope="session")
def ui_capable_role_session(hub_devices: dict[str, Any]) -> str | None:
    """Session-scoped lookup mirroring `ui_capable_role` but non-skipping.

    Used by the `camera` session fixture so it doesn't depend on a
    test-scoped skip.
    """
    for role in UI_CAPABLE_ROLES:
        if role in hub_devices:
            return role
    return None


@pytest.fixture(scope="session", autouse=True)
def _ocr_warm() -> None:
    """Pay easyocr's ~100 MB / cold-start cost ONCE per session.

    Subsequent `ocr_text()` calls hit the cached reader and return quickly.
    Swallows errors — if OCR isn't installed, warm is a no-op.
    """
    try:
        ocr_mod.warm()
    except Exception:  # noqa: BLE001 — belt: never block the suite on OCR init
        pass


@pytest.fixture(scope="session")
def _ui_screen_kept_on(
    ui_capable_role_session: str | None, hub_devices: dict[str, Any]
) -> Iterator[None]:
    """Keep the OLED on throughout the UI tier so input events aren't dropped.

    Why: `InputBroker::handleInputEvent` (src/input/InputBroker.cpp:118-122)
    silently DROPS any event that arrives while the screen is off — it just
    wakes the screen and returns. Every first event in each test would
    disappear. We set `display.screen_on_secs = 86400` at session start
    (effectively "always on" for the test window) and restore the prior
    value at teardown.
    """
    if ui_capable_role_session is None:
        yield
        return

    hub_entry = hub_devices[ui_capable_role_session]
    port = hub_entry.get("port") if isinstance(hub_entry, dict) else hub_entry
    if not port:
        yield
        return

    original: int | None = None
    try:
        current = admin_mod.get_config(section="display", port=port)
        original = int(
            current.get("config", {}).get("display", {}).get("screen_on_secs") or 0
        )
    except Exception:  # noqa: BLE001
        pass

    try:
        admin_mod.set_config("display.screen_on_secs", 86400, port=port)
        # Send one wake event so the screen is actually ON going into the
        # first test. The event itself gets dropped (screenWasOff), but the
        # wake side-effect sticks.
        try:
            admin_mod.send_input_event(event_code=int(InputEventCode.FN_F1), port=port)
        except Exception:  # noqa: BLE001
            pass
        time.sleep(1.5)  # Let the screen finish its wake transition.
    except (
        Exception
    ):  # noqa: BLE001 — best-effort; ui_home_state surfaces the real error
        pass

    try:
        yield
    finally:
        if original is not None:
            try:
                admin_mod.set_config("display.screen_on_secs", original, port=port)
            except Exception:  # noqa: BLE001
                pass


# ---------- Per-test capture + transcript ----------------------------------


class FrameCapture:
    """Per-test capture recorder. Created once per test via the
    `frame_capture` fixture; call with a label to snapshot the screen.
    """

    def __init__(
        self,
        cam: camera_mod.CameraBackend,
        dir_path: Path,
        lines: list[str],
        nodeid: str,
    ) -> None:
        self._cam = cam
        self._dir = dir_path
        self._lines = lines
        self._nodeid = nodeid
        self._step = 0
        self.captures: list[dict[str, Any]] = []
        self._transcript_path = dir_path / "transcript.md"
        self._dir.mkdir(parents=True, exist_ok=True)
        self._transcript_path.write_text(
            f"# {nodeid} — {time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}\n\n",
            encoding="utf-8",
        )

    def __call__(self, label: str) -> dict[str, Any]:
        self._step += 1
        stem = f"{self._step:03d}-{re.sub(r'[^a-zA-Z0-9_-]+', '-', label)}"
        png_path = self._dir / f"{stem}.png"
        ocr_path = self._dir / f"{stem}.ocr.txt"

        try:
            png = self._cam.capture()
        except Exception as exc:  # noqa: BLE001
            png = b""
            ocr_str = f"[capture error: {exc}]"
        else:
            camera_mod.save_capture(png, png_path)
            try:
                ocr_str = ocr_mod.ocr_text(png)
            except Exception as exc:  # noqa: BLE001
                ocr_str = f"[ocr error: {exc}]"
            ocr_path.write_text(ocr_str or "", encoding="utf-8")

        frame = get_current_frame(self._lines)
        entry: dict[str, Any] = {
            "step": self._step,
            "label": label,
            "png_path": str(png_path) if png else None,
            "ocr_text": ocr_str,
            "frame": (
                {
                    "idx": frame.idx,
                    "name": frame.name,
                    "reason": frame.reason,
                }
                if frame is not None
                else None
            ),
        }
        self.captures.append(entry)

        with self._transcript_path.open("a", encoding="utf-8") as fh:
            frame_str = (
                f"frame {frame.idx}/{frame.count} name={frame.name} reason={frame.reason}"
                if frame is not None
                else "frame <none>"
            )
            ocr_summary = (ocr_str or "").replace("\n", " / ")[:80]
            fh.write(
                f"{self._step}. **{label}** — {frame_str} — OCR: `{ocr_summary}`\n"
            )
        return entry


@pytest.fixture
def frame_capture(
    request: pytest.FixtureRequest,
    camera: camera_mod.CameraBackend,
    session_seed: str,
) -> Iterator[FrameCapture]:
    nodeid = _sanitize_nodeid(request.node.nodeid)
    dir_path = CAPTURES_ROOT / session_seed / nodeid
    # Fresh directory per test run so reruns don't mix old and new images.
    if dir_path.exists():
        shutil.rmtree(dir_path)

    lines = getattr(request.node, "_debug_log_buffer", [])
    fc = FrameCapture(camera, dir_path, lines, nodeid)
    # Stash so pytest_runtest_makereport can embed captures in HTML extras.
    request.node._ui_captures = fc.captures  # type: ignore[attr-defined]
    yield fc


# ---------- Pre-test home-state reset --------------------------------------


def _send_event(port: str, event: InputEventCode) -> None:
    try:
        admin_mod.send_input_event(event_code=int(event), port=port)
    except Exception:  # noqa: BLE001
        # Treat a failed event as soft — the subsequent frame-log assertion
        # surfaces the real problem with better context.
        pass


@pytest.fixture(autouse=True)
def ui_home_state(
    request: pytest.FixtureRequest,
    hub_devices: dict[str, Any],
    _ui_screen_kept_on: None,
) -> Iterator[None]:
    """Before every UI test, jump to frame 0 (usually `home`) via FN_F1 and
    confirm the device emitted the expected frame log.

    Why FN_F1 (not BACK): FN_F1 maps to `switchToFrame(0)` and ALWAYS
    produces a `reason=fn_f1` log line, regardless of whatever frame the
    prior test left us on. BACK is context-sensitive (dismisses overlays
    on some frames, no-op on others) and can silently fail to transition.

    This fixture doubles as the macro-presence detector: if no `fn_f1`
    log arrives within 5 s, the firmware almost certainly wasn't baked
    with `USERPREFS_UI_TEST_LOG`. Skip the tier with an actionable hint
    instead of letting every test body fail with a confusing assertion.

    Autouse scope is restricted to `tests/ui/` by virtue of this fixture
    living in that directory's conftest.py — no explicit nodeid guard
    needed (and earlier attempts at one were wrong, matching `/tests/ui/`
    against a nodeid that has no leading slash).
    """
    role = next((r for r in UI_CAPABLE_ROLES if r in hub_devices), None)
    if role is None:
        yield
        return

    hub_entry = hub_devices[role]
    port = hub_entry.get("port") if isinstance(hub_entry, dict) else hub_entry
    lines: list[str] = getattr(request.node, "_debug_log_buffer", [])
    start_len = len(lines)

    # First: a wake event. The screen should already be kept on by
    # `_ui_screen_kept_on`, but belt + suspenders — if it somehow
    # powered off (sleep after factory_reset, etc.), this first FN_F1
    # gets dropped by InputBroker's screenWasOff guard. That's fine;
    # the second FN_F1 below lands cleanly.
    _send_event(port, InputEventCode.FN_F1)
    time.sleep(0.4)
    _send_event(port, InputEventCode.FN_F1)

    # Wait for the fn_f1 transition log. Any new `reason=fn_f1` line
    # after call-start counts — we don't care about the name (it should
    # be `home` or `deviceFocused` depending on board-specific frame order).
    from ._screen_log import wait_for_reason

    try:
        wait_for_reason(lines, "fn_f1", timeout_s=5.0)
    except TimeoutError:
        # One more try — FreeRTOS queue may be draining slowly.
        _send_event(port, InputEventCode.FN_F1)
        try:
            wait_for_reason(lines, "fn_f1", timeout_s=5.0)
        except TimeoutError:
            # Look at what the _debug_log_buffer actually contains to
            # disambiguate "macro off" from "macro on but event lost".
            frame_lines = [ln for ln in lines[start_len:] if "Screen: frame" in ln]
            processing_lines = [
                ln for ln in lines[start_len:] if "Processing input event" in ln
            ]
            if frame_lines:
                pytest.skip(
                    f"ui_home_state: events fire but none reach Screen "
                    f"(saw {len(frame_lines)} frame line(s), "
                    f"{len(processing_lines)} admin inject(s)). "
                    f"Device may be in an unusual state — try `--force-bake`."
                )
            else:
                pytest.skip(
                    "ui_home_state: no `Screen: frame` log after FN_F1. "
                    "Firmware not baked with USERPREFS_UI_TEST_LOG — "
                    "run with `--force-bake` to reflash, or verify the "
                    "macro is active in the bake."
                )
    yield


# ---------- Small helpers reused by tests ---------------------------------


def send_event(
    port: str, event: InputEventCode | int | str, **kwargs: Any
) -> dict[str, Any]:
    """Thin wrapper so tests read `send_event(port, InputEventCode.RIGHT)`."""
    return admin_mod.send_input_event(event_code=event, port=port, **kwargs)


__all__ = [
    "FrameCapture",
    "UI_CAPABLE_ROLES",
    "send_event",
    "wait_for_frame",
    "FrameEvent",
]


# Make the helpers discoverable to test modules via `from .conftest import …`.
# pytest auto-loads conftest.py, but the symbols above are also re-exported
# for readability in the test files.
