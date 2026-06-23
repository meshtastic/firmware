"""MJPEG camera streaming.

Opens a capture device by OpenCV index and yields a ``multipart/x-mixed-replace``
JPEG stream for an ``<img>`` tag. Rotation is applied client-side (CSS), so the
stream is raw. OpenCV (the ``[ui]`` extra) is optional — without it, ``probe``
reports the error and the stream endpoint 503s instead of crashing.
"""

from __future__ import annotations

import asyncio
import logging
from typing import AsyncIterator

log = logging.getLogger("meshtastic_mcp.web.camera_stream")

BOUNDARY = "frame"
_FPS = 10.0


def _import_cv2():
    try:
        import cv2  # type: ignore

        return cv2
    except Exception:  # noqa: BLE001
        return None


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
