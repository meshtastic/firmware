"""OCR wrapper for UI tests + the `capture_screen` tool.

Auto-selects a reader in priority order:
  1. `easyocr` (deep-learning, high quality on OLED screens — but ~100 MB
     model download on first use).
  2. `pytesseract` (requires system `tesseract` binary on PATH).
  3. `null` — returns `""` with a warning. Tests fall back to log + image
     evidence when OCR is unavailable.

Override via `MESHTASTIC_UI_OCR_BACKEND=easyocr|pytesseract|null|auto`
(default `auto`).

`ocr_text(png_bytes) -> str` is the only public entry point. The reader is
constructed lazily on first call and cached, so the easyocr cold-start cost
only hits once per process.
"""

from __future__ import annotations

import functools
import logging
import os
import shutil
import sys
from typing import Callable

log = logging.getLogger(__name__)


def _backend_choice() -> str:
    return os.environ.get("MESHTASTIC_UI_OCR_BACKEND", "auto").lower()


@functools.lru_cache(maxsize=1)
def _reader() -> tuple[str, Callable[[bytes], str]]:
    """Return `(backend_name, callable)` for whichever OCR is available."""
    choice = _backend_choice()

    def _easyocr() -> tuple[str, Callable[[bytes], str]]:
        import easyocr  # type: ignore[import-untyped]  # noqa: PLC0415
        import numpy as np  # type: ignore[import-untyped]  # noqa: PLC0415

        reader = easyocr.Reader(["en"], gpu=False, verbose=False)

        def _run(png: bytes) -> str:
            try:
                import cv2  # type: ignore[import-untyped]  # noqa: PLC0415

                arr = np.frombuffer(png, dtype=np.uint8)
                img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
            except ImportError:
                # Fall back to PIL if cv2 isn't around.
                from io import BytesIO  # noqa: PLC0415

                from PIL import Image  # type: ignore[import-untyped]  # noqa: PLC0415

                img = np.array(Image.open(BytesIO(png)).convert("RGB"))
            try:
                results = reader.readtext(img, detail=0, paragraph=True)
            except Exception as exc:  # noqa: BLE001
                log.warning("easyocr failed: %s", exc)
                return ""
            return "\n".join(str(r) for r in results)

        return "easyocr", _run

    def _pytesseract() -> tuple[str, Callable[[bytes], str]]:
        from io import BytesIO  # noqa: PLC0415

        import pytesseract  # type: ignore[import-untyped]  # noqa: PLC0415
        from PIL import Image  # type: ignore[import-untyped]  # noqa: PLC0415

        if shutil.which("tesseract") is None:
            raise ImportError("`tesseract` binary not on PATH")

        def _run(png: bytes) -> str:
            try:
                return str(pytesseract.image_to_string(Image.open(BytesIO(png))))
            except Exception as exc:  # noqa: BLE001
                log.warning("pytesseract failed: %s", exc)
                return ""

        return "pytesseract", _run

    def _null() -> tuple[str, Callable[[bytes], str]]:
        log.warning(
            "OCR backend is null; install easyocr or tesseract for text extraction"
        )
        return "null", lambda _png: ""

    if choice == "easyocr":
        return _easyocr()
    if choice == "pytesseract":
        return _pytesseract()
    if choice == "null":
        return _null()
    if choice != "auto":
        print(
            f"[ocr] unknown MESHTASTIC_UI_OCR_BACKEND={choice!r}; falling back to auto",
            file=sys.stderr,
        )

    # auto mode
    try:
        return _easyocr()
    except ImportError:
        pass
    try:
        return _pytesseract()
    except ImportError:
        pass
    return _null()


def ocr_text(png_bytes: bytes) -> str:
    """Run OCR on a PNG-encoded image and return the decoded text (possibly empty)."""
    if not png_bytes:
        return ""
    _, run = _reader()
    return run(png_bytes)


def backend_name() -> str:
    """Return the currently-selected backend name, initializing if necessary."""
    name, _ = _reader()
    return name


def warm() -> None:
    """Run one dummy inference so the easyocr cold-start cost is paid upfront.

    Pytest session fixture calls this once so the first real capture doesn't
    eat the model-load latency.
    """
    # A 64×32 white PNG — decodes clean, no text to extract.
    white_png = bytes.fromhex(
        "89504e470d0a1a0a0000000d49484452000000400000002008060000007ccac28e"
        "0000001c49444154785eedc1010d000000c2a0f74f6d0d370000000000000080"
        "0b010000ffff030000000000000049454e44ae426082"
    )
    try:
        ocr_text(white_png)
    except Exception as exc:  # noqa: BLE001
        log.warning("ocr.warm() failed: %s", exc)


__all__ = ["backend_name", "ocr_text", "warm"]
