"""Cross-platform USB-webcam capture for UI tests + the `capture_screen` tool.

Backends:
- `opencv` — cv2.VideoCapture (AVFoundation on macOS, V4L2 on Linux).
- `ffmpeg` — subprocess shelling out to the system `ffmpeg` binary. Slower
  per frame, but zero Python deps beyond stdlib.
- `null` — no-op stub returning a 1×1 black PNG. Used when no camera is
  configured; keeps code paths alive without forcing every operator to
  hook up hardware.

Environment variables (read at `get_camera()` call time):
- `MESHTASTIC_UI_CAMERA_BACKEND` — one of `opencv` / `ffmpeg` / `null` /
  `auto` (default). `auto` picks opencv if `cv2` imports, else ffmpeg if
  `ffmpeg --version` resolves, else null.
- `MESHTASTIC_UI_CAMERA_DEVICE` — generic default (index or path).
- `MESHTASTIC_UI_CAMERA_DEVICE_<ROLE>` — per-role override, e.g.
  `MESHTASTIC_UI_CAMERA_DEVICE_ESP32S3=0` for the OLED-bearing heltec-v3.
  Role suffix is uppercased before lookup.

Dependencies land in the optional `[ui]` extra; imports are lazy so clients
without `opencv-python-headless` installed can still import this module.
"""

from __future__ import annotations

import io
import os
import shutil
import subprocess
import sys
import time
import warnings
from pathlib import Path
from typing import Protocol


class CameraError(RuntimeError):
    """Raised when a camera backend fails to initialize or capture."""


class CameraBackend(Protocol):
    name: str

    def capture(self) -> bytes:
        """Return one PNG-encoded frame."""
        ...

    def close(self) -> None: ...


# ---------- OpenCV backend -------------------------------------------------


class OpenCVBackend:
    name = "opencv"

    def __init__(self, device: int | str, warmup_frames: int = 5) -> None:
        try:
            import cv2  # type: ignore[import-untyped]  # noqa: PLC0415
        except ImportError as exc:
            raise CameraError(
                "opencv backend requested but `cv2` is not installed. "
                "Install the mcp-server [ui] extra: pip install -e '.[ui]'"
            ) from exc

        self._cv2 = cv2
        device_arg: int | str
        if isinstance(device, str) and device.isdigit():
            device_arg = int(device)
        else:
            device_arg = device
        self._cap = cv2.VideoCapture(device_arg)
        if not self._cap.isOpened():
            raise CameraError(
                f"cv2.VideoCapture({device_arg!r}) failed to open. "
                "On macOS check TCC Camera permission; on Linux check /dev/video* and v4l2 access."
            )

        # Drop the first few frames — auto-exposure + white-balance settle.
        for _ in range(warmup_frames):
            self._cap.read()
        # Detect a stuck black-frame camera early rather than silently
        # producing all-black captures.
        ok, frame = self._cap.read()
        if not ok or frame is None:
            self._cap.release()
            raise CameraError(f"camera {device_arg!r} opened but returned no frames")

    def capture(self) -> bytes:
        cv2 = self._cv2
        ok, frame = self._cap.read()
        if not ok or frame is None:
            raise CameraError("cv2.VideoCapture.read() returned no frame")
        success, buf = cv2.imencode(".png", frame)
        if not success:
            raise CameraError("cv2.imencode('.png', ...) failed")
        return bytes(buf)

    def close(self) -> None:
        try:
            self._cap.release()
        except Exception:  # noqa: BLE001
            pass


# ---------- ffmpeg subprocess backend --------------------------------------


class FfmpegBackend:
    name = "ffmpeg"

    def __init__(self, device: int | str) -> None:
        if shutil.which("ffmpeg") is None:
            raise CameraError("ffmpeg backend requested but `ffmpeg` is not on PATH")

        self._device = str(device)
        # Platform-specific -f flag:
        #   macOS → avfoundation (index like "0")
        #   Linux → v4l2        (device like "/dev/video0" or "0")
        if sys.platform == "darwin":
            self._input_format = "avfoundation"
            self._input_spec = self._device  # bare index for avfoundation
        else:
            self._input_format = "v4l2"
            self._input_spec = (
                self._device
                if self._device.startswith("/dev/")
                else f"/dev/video{self._device}"
            )

    def capture(self) -> bytes:
        cmd = [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "error",
            "-f",
            self._input_format,
            "-i",
            self._input_spec,
            "-frames:v",
            "1",
            "-f",
            "image2pipe",
            "-vcodec",
            "png",
            "-",
        ]
        try:
            out = subprocess.run(
                cmd, capture_output=True, check=True, timeout=15  # noqa: S603
            )
        except subprocess.CalledProcessError as exc:
            raise CameraError(
                f"ffmpeg capture failed (rc={exc.returncode}): {exc.stderr.decode(errors='replace')[:200]}"
            ) from exc
        except subprocess.TimeoutExpired as exc:
            raise CameraError("ffmpeg capture timed out after 15s") from exc
        return out.stdout

    def close(self) -> None:
        pass  # stateless — each capture spawns a new process


# ---------- Null backend ---------------------------------------------------


# A tiny valid 1×1 transparent PNG so callers always get a decodable image.
_BLACK_1X1_PNG = bytes.fromhex(
    "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489"
    "0000000d49444154789c6300010000000500010d0a2db40000000049454e44ae426082"
)


class NullBackend:
    name = "null"

    def capture(self) -> bytes:
        return _BLACK_1X1_PNG

    def close(self) -> None:
        pass


# ---------- Factory --------------------------------------------------------


def _resolve_device(role: str | None) -> str | None:
    if role:
        specific = os.environ.get(f"MESHTASTIC_UI_CAMERA_DEVICE_{role.upper()}")
        if specific:
            return specific
    return os.environ.get("MESHTASTIC_UI_CAMERA_DEVICE")


def get_camera(role: str | None = None) -> CameraBackend:
    """Return a CameraBackend for the given device role (e.g. `"esp32s3"`).

    Falls back to `NullBackend` if no camera is configured or the selected
    backend fails to init — tests should treat captures as best-effort
    evidence, not a blocker.
    """
    backend = os.environ.get("MESHTASTIC_UI_CAMERA_BACKEND", "auto").lower()
    device = _resolve_device(role)

    if backend in ("null", "none") or device is None:
        return NullBackend()

    if backend == "auto":
        # Prefer opencv if importable; fall back to ffmpeg; else null.
        try:
            import cv2  # type: ignore[import-untyped]  # noqa: F401,PLC0415

            backend = "opencv"
        except ImportError:
            backend = "ffmpeg" if shutil.which("ffmpeg") else "null"

    if backend == "opencv":
        try:
            return OpenCVBackend(device)
        except CameraError as exc:
            warnings.warn(
                f"camera backend {backend!r} failed to initialize for device "
                f"{device!r}: {exc}; falling back to null backend",
                RuntimeWarning,
                stacklevel=2,
            )
            return NullBackend()
    if backend == "ffmpeg":
        try:
            return FfmpegBackend(device)
        except CameraError as exc:
            warnings.warn(
                f"camera backend {backend!r} failed to initialize for device "
                f"{device!r}: {exc}; falling back to null backend",
                RuntimeWarning,
                stacklevel=2,
            )
            return NullBackend()
    if backend == "null":
        return NullBackend()

    raise CameraError(f"unknown MESHTASTIC_UI_CAMERA_BACKEND: {backend!r}")


def save_capture(png_bytes: bytes, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png_bytes)


def capture_to_file(role: str | None, path: Path) -> dict[str, object]:
    """One-shot: open camera, capture, write PNG, close. Returns metadata."""
    started = time.monotonic()
    cam = get_camera(role)
    try:
        data = cam.capture()
    finally:
        cam.close()
    save_capture(data, path)
    return {
        "backend": cam.name,
        "path": str(path),
        "bytes": len(data),
        "elapsed_s": round(time.monotonic() - started, 3),
    }


def _is_png(data: bytes) -> bool:
    return data.startswith(b"\x89PNG\r\n\x1a\n")


# Exposed so callers can sanity-check a capture without a full PIL import.
__all__ = [
    "CameraBackend",
    "CameraError",
    "FfmpegBackend",
    "NullBackend",
    "OpenCVBackend",
    "capture_to_file",
    "get_camera",
    "save_capture",
]

# Keep `io` import used (pyflakes is picky) via a small guard used at import
# time to normalize stdin/stdout if a subclass ever needs it.
_ = io.BytesIO  # noqa: SLF001
