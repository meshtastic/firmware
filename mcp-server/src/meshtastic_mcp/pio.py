"""Subprocess wrappers around the `pio` CLI.

Every PlatformIO interaction in this package funnels through `run()` so we
have a single place that owns timeouts, buffer sizes, JSON parsing, and the
"stderr on exit-0 is informational" convention.
"""

from __future__ import annotations

import json
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

from . import config

# 10 MB matches the reference impl (jl-codes/platformio-mcp). Build output can
# be hundreds of KB; we'd rather keep it in memory than truncate.
_MAX_BUFFER = 10 * 1024 * 1024

# Per-operation defaults (seconds). None = no timeout.
TIMEOUT_DEFAULT = 120
TIMEOUT_PROJECT_CONFIG = 60
TIMEOUT_DEVICE_LIST = 15
TIMEOUT_BUILD = 900
TIMEOUT_UPLOAD = 600


class PioError(RuntimeError):
    """pio exited non-zero."""

    def __init__(self, args: Sequence[str], returncode: int, stdout: str, stderr: str):
        self.args = list(args)
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr
        tail = (stderr or stdout).strip().splitlines()[-20:]
        super().__init__(
            f"pio {' '.join(args)} failed with exit {returncode}:\n" + "\n".join(tail)
        )


class PioTimeout(RuntimeError):
    """pio did not return within the timeout."""


@dataclass
class PioResult:
    args: list[str]
    returncode: int
    stdout: str
    stderr: str
    duration_s: float


def run(
    args: Sequence[str],
    *,
    cwd: Path | None = None,
    timeout: float | None = TIMEOUT_DEFAULT,
    check: bool = True,
) -> PioResult:
    """Invoke `pio <args>` and return captured output.

    `cwd` defaults to the firmware root. `check=True` raises `PioError` on
    non-zero exit; set `check=False` to inspect `returncode` manually.
    """
    binary = str(config.pio_bin())
    work_dir = cwd or config.firmware_root()
    full = [binary, *args]
    t0 = time.monotonic()
    try:
        proc = subprocess.run(
            full,
            cwd=str(work_dir),
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        raise PioTimeout(f"pio {' '.join(args)} timed out after {timeout}s") from exc
    duration = time.monotonic() - t0

    result = PioResult(
        args=list(args),
        returncode=proc.returncode,
        stdout=proc.stdout or "",
        stderr=proc.stderr or "",
        duration_s=duration,
    )
    if check and result.returncode != 0:
        raise PioError(args, result.returncode, result.stdout, result.stderr)
    return result


def run_json(
    args: Sequence[str],
    *,
    cwd: Path | None = None,
    timeout: float | None = TIMEOUT_DEFAULT,
):
    """Run pio with `--json-output` appended and parse the result."""
    full = list(args)
    if "--json-output" not in full:
        full.append("--json-output")
    res = run(full, cwd=cwd, timeout=timeout, check=True)
    if not res.stdout.strip():
        raise PioError(args, 0, res.stdout, res.stderr or "pio returned empty output")
    try:
        return json.loads(res.stdout)
    except json.JSONDecodeError as exc:
        raise PioError(
            args, 0, res.stdout[:2000], f"invalid JSON from pio: {exc}"
        ) from exc


def tail_lines(text: str, n: int = 200) -> str:
    """Last `n` lines of `text`, joined with newlines. Empty string stays empty."""
    if not text:
        return ""
    lines = text.splitlines()
    return "\n".join(lines[-n:])
