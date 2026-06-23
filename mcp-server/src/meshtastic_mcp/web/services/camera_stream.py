"""MJPEG camera streaming.

Opens a capture device by OpenCV index and yields a ``multipart/x-mixed-replace``
JPEG stream for an ``<img>`` tag. Rotation is applied client-side (CSS), so the
stream is raw. OpenCV (the ``[ui]`` extra) is optional — without it, ``probe``
reports the error and the stream endpoint 503s instead of crashing.
"""

from __future__ import annotations

import asyncio
import json
import logging
import subprocess
import sys
from pathlib import Path
from typing import AsyncIterator

log = logging.getLogger("meshtastic_mcp.web.camera_stream")

BOUNDARY = "frame"
_FPS = 10.0
_MAX_INDEX = 10  # don't probe past this


def _import_cv2():
    try:
        import cv2  # type: ignore

        try:  # quiet the noisy "can't open camera" warnings during probing
            cv2.utils.logging.setLogLevel(cv2.utils.logging.LOG_LEVEL_SILENT)
        except Exception:  # noqa: BLE001
            pass
        return cv2
    except Exception:  # noqa: BLE001
        return None


def _enumerate_names() -> list[tuple[int, str]]:
    """Best-effort camera names from the OS, WITHOUT activating any device.
    Returns ``[(index, name), ...]``. The index is the OpenCV capture index
    (enumeration order on macOS, the videoN number on Linux)."""
    if sys.platform == "darwin":
        try:
            out = subprocess.run(
                ["system_profiler", "SPCameraDataType", "-json"],
                capture_output=True,
                text=True,
                timeout=10,
            )
            items = json.loads(out.stdout).get("SPCameraDataType", [])
            return [(i, it.get("_name", f"camera {i}")) for i, it in enumerate(items)]
        except Exception:  # noqa: BLE001
            return []
    if sys.platform.startswith("linux"):
        out: list[tuple[int, str]] = []
        for node in sorted(Path("/sys/class/video4linux").glob("video*")):
            try:
                idx = int(node.name[len("video") :])
                name = (node / "name").read_text().strip()
                out.append((idx, name or f"video{idx}"))
            except Exception:  # noqa: BLE001
                continue
        return out
    return []


def _probe_resolution(cv2, index: int) -> dict | None:
    """Open a capture index briefly to confirm it works + read its resolution.
    Activates the camera momentarily; returns None if it won't open/read."""
    try:
        cap = cv2.VideoCapture(index)
    except Exception:  # noqa: BLE001
        return None
    try:
        if not cap.isOpened():
            return None
        ok, _ = cap.read()
        if not ok:
            return None
        return {
            "width": int(cap.get(cv2.CAP_PROP_FRAME_WIDTH) or 0),
            "height": int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT) or 0),
        }
    finally:
        cap.release()


def discover(skip: set[str] | None = None, probe_resolution: bool = True) -> dict:
    """Enumerate attached cameras.

    Name enumeration needs no OpenCV (works even without the ``[ui]`` extra), so
    discovery is useful before streaming is installed. When cv2 IS present, each
    not-in-use index is briefly opened to confirm it works and read its
    resolution. ``skip`` is the set of indices already bound to a FleetSuite
    camera — they're marked ``in_use`` and not re-opened (their stream owns them).
    """
    skip = skip or set()
    cv2 = _import_cv2()
    named = _enumerate_names()

    # If the OS gave us no names but cv2 is here, fall back to index probing.
    if not named and cv2 is not None:
        named = [(i, f"camera {i}") for i in range(_MAX_INDEX)]

    cameras: list[dict] = []
    for index, name in named:
        if index >= _MAX_INDEX:
            continue
        entry: dict = {"index": index, "name": name, "in_use": str(index) in skip}
        if cv2 is not None and not entry["in_use"] and probe_resolution:
            res = _probe_resolution(cv2, index)
            if res is None:
                # Named by the OS but cv2 couldn't open it — surface, don't drop.
                entry["unavailable"] = True
            else:
                entry.update(res)
        cameras.append(entry)

    return {
        "available": True,
        "cv2": cv2 is not None,
        "cameras": cameras,
    }


def probe(device_index: str) -> dict:
    """Can we open this device? Returns ``{ok, error}``."""
    cv2 = _import_cv2()
    if cv2 is None:
        return {"ok": False, "error": "opencv not installed (pip install -e '.[ui]')"}
    try:
        cap = cv2.VideoCapture(int(device_index))
    except (ValueError, TypeError):
        return {"ok": False, "error": f"invalid device index: {device_index!r}"}
    try:
        if not cap.isOpened():
            return {"ok": False, "error": "device did not open (in use or absent?)"}
        ok, _ = cap.read()
        return {"ok": bool(ok), "error": None if ok else "no frame from device"}
    finally:
        cap.release()


async def mjpeg(device_index: str) -> AsyncIterator[bytes]:
    """Async MJPEG frame generator. Reads happen in a worker thread so the event
    loop is never blocked on the camera."""
    cv2 = _import_cv2()
    if cv2 is None:
        return
    cap = await asyncio.to_thread(cv2.VideoCapture, int(device_index))
    try:
        if not cap.isOpened():
            return
        while True:
            ok, frame = await asyncio.to_thread(cap.read)
            if not ok:
                break
            enc_ok, buf = await asyncio.to_thread(cv2.imencode, ".jpg", frame)
            if enc_ok:
                jpg = buf.tobytes()
                yield (
                    b"--" + BOUNDARY.encode() + b"\r\n"
                    b"Content-Type: image/jpeg\r\n"
                    b"Content-Length: " + str(len(jpg)).encode() + b"\r\n\r\n"
                    + jpg + b"\r\n"
                )
            await asyncio.sleep(1.0 / _FPS)
    finally:
        cap.release()
