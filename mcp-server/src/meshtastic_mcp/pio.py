"""Subprocess wrappers around the `pio` CLI.

Every PlatformIO interaction in this package funnels through `run()` so we
have a single place that owns timeouts, buffer sizes, JSON parsing, and the
"stderr on exit-0 is informational" convention.

`run()` has two execution paths:

* Fast path (default): `subprocess.run(capture_output=True)` — buffered, one
  return; fine for sub-second pio calls like `pio --version` or
  `pio project config --json-output`.
* Streaming path: when the `MESHTASTIC_MCP_FLASH_LOG` env var is set, each
  output line is tee'd to that file as it arrives via a threaded reader.
  The TUI tails the file to give live flash progress — otherwise a 3-minute
  `pio run -t upload` is completely silent to the operator.

`hw_tools.py` shares the streaming helper via `pio._run_capturing()` so
esptool/nrfutil/picotool output also streams when the env var is set.
"""

from __future__ import annotations

import json
import os
import subprocess
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence, TextIO

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


_FLASH_LOG_ENV = "MESHTASTIC_MCP_FLASH_LOG"


def _flash_log_path() -> Path | None:
    """Return the path to tee subprocess output to, or None if streaming off.

    Controlled by `MESHTASTIC_MCP_FLASH_LOG`. `run-tests.sh` sets this to
    `tests/flash.log`; the TUI tails that file so `pio run -t upload` shows
    live progress in the pytest pane.
    """
    raw = os.environ.get(_FLASH_LOG_ENV)
    if not raw:
        return None
    return Path(raw)


def _run_capturing(
    argv: Sequence[str],
    *,
    cwd: Path | None = None,
    timeout: float | None = None,
    tee_header: str | None = None,
    extra_env: dict[str, str] | None = None,
) -> tuple[int, str, str, float]:
    """Run a subprocess, capture stdout+stderr, optionally tee to the flash log.

    Returns `(returncode, stdout_str, stderr_str, duration_s)`. Raises
    `subprocess.TimeoutExpired` on timeout (callers map this to their own
    domain-specific error).

    `extra_env` merges into the subprocess environment (parent env stays
    intact). Used for `PLATFORMIO_BUILD_FLAGS=-DDEBUG_HEAP=1` and similar.

    Fast path: `subprocess.run(capture_output=True)` when no flash log is
    configured (unchanged behavior).

    Streaming path: `Popen` with line-buffered stdout+stderr pipes; two
    reader threads accumulate into result strings AND append each line to
    the flash log file. Stdout and stderr stay separate in the return value
    (so `stderr_tail` still means stderr), but are interleaved in the log
    file in the order they arrived — that's what a human wants to read.
    """
    log_path = _flash_log_path()
    t0 = time.monotonic()
    env = None
    if extra_env:
        env = {**os.environ, **extra_env}

    if log_path is None:
        # Fast path — unchanged.
        proc = subprocess.run(
            list(argv),
            cwd=str(cwd) if cwd else None,
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env,
        )
        return (
            proc.returncode,
            proc.stdout or "",
            proc.stderr or "",
            time.monotonic() - t0,
        )

    # Streaming path: line-buffered Popen, threaded readers, tee to file.
    # Ensure parent directory exists so the first tee write doesn't fail.
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_fh: TextIO | None = None
    try:
        log_fh = log_path.open("a", encoding="utf-8")
    except OSError:
        pass
    # Append mode: the TUI truncates on startup, the session may produce
    # many tee'd commands (erase + flash + factory-reset response), and
    # we want all of them chronologically in one log.
    proc = subprocess.Popen(  # noqa: S603
        list(argv),
        cwd=str(cwd) if cwd else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,  # line-buffered
        env=env,
    )
    stdout_chunks: list[str] = []
    stderr_chunks: list[str] = []
    log_lock = threading.Lock()

    def _append_log(line: str) -> None:
        # Hold the lock briefly to serialize interleaved stdout/stderr writes
        # so a half-written line from one stream doesn't get garbled by the
        # other.
        nonlocal log_fh
        with log_lock:
            if log_fh is None:
                return
            try:
                log_fh.write(line)
                log_fh.flush()
            except OSError:
                # Log file disappeared (umount, operator deleted the dir).
                # Don't let that bubble up — the subprocess output is still
                # collected in-memory for the return value.
                try:
                    log_fh.close()
                except OSError:
                    pass
                log_fh = None

    def _tee(stream, sink: list[str]) -> None:
        try:
            for line in stream:
                sink.append(line)
                _append_log(line)
        except Exception:
            pass

    # Header line so the operator can tell commands apart in the log.
    if tee_header:
        _append_log(f"\n--- {tee_header} (start)\n")

    assert proc.stdout is not None and proc.stderr is not None
    t_out = threading.Thread(
        target=_tee, args=(proc.stdout, stdout_chunks), daemon=True
    )
    t_err = threading.Thread(
        target=_tee, args=(proc.stderr, stderr_chunks), daemon=True
    )
    t_out.start()
    t_err.start()

    # `Popen.wait` with a timeout is the cleanest way to get TimeoutExpired.
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        # Drain readers before re-raising so we don't leave threads behind.
        t_out.join(timeout=2)
        t_err.join(timeout=2)
        raise

    t_out.join()
    t_err.join()
    duration = time.monotonic() - t0

    if tee_header:
        _append_log(f"--- {tee_header} (exit {proc.returncode} in {duration:.1f}s)\n")

    try:
        return (
            proc.returncode,
            "".join(stdout_chunks),
            "".join(stderr_chunks),
            duration,
        )
    finally:
        if log_fh is not None:
            try:
                log_fh.close()
            except OSError:
                pass


def run(
    args: Sequence[str],
    *,
    cwd: Path | None = None,
    timeout: float | None = TIMEOUT_DEFAULT,
    check: bool = True,
    extra_env: dict[str, str] | None = None,
) -> PioResult:
    """Invoke `pio <args>` and return captured output.

    `cwd` defaults to the firmware root. `check=True` raises `PioError` on
    non-zero exit; set `check=False` to inspect `returncode` manually.

    `extra_env` merges into the subprocess environment — used for
    `PLATFORMIO_BUILD_FLAGS=-DDEBUG_HEAP=1` and similar build-time
    toggles that can't be expressed as command-line args.

    If `MESHTASTIC_MCP_FLASH_LOG` is set, output is also tee'd to that file
    line-by-line as it arrives (for live flash progress in the TUI).
    """
    binary = str(config.pio_bin())
    work_dir = cwd or config.firmware_root()
    full = [binary, *args]
    try:
        rc, stdout, stderr, duration = _run_capturing(
            full,
            cwd=work_dir,
            timeout=timeout,
            tee_header=f"pio {' '.join(args)}",
            extra_env=extra_env,
        )
    except subprocess.TimeoutExpired as exc:
        raise PioTimeout(f"pio {' '.join(args)} timed out after {timeout}s") from exc

    result = PioResult(
        args=list(args),
        returncode=rc,
        stdout=stdout,
        stderr=stderr,
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
