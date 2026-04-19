"""`pio.py` subprocess wrapper: error paths, tailing, JSON parsing.

Uses a real `pio` install only for the happy-path `--version`; error paths are
exercised with a deliberately-broken `MESHTASTIC_PIO_BIN` override.
"""

from __future__ import annotations

import pytest
from meshtastic_mcp import pio


def test_tail_lines_keeps_last_n() -> None:
    text = "\n".join(f"line-{i}" for i in range(1, 11))
    assert pio.tail_lines(text, 3) == "line-8\nline-9\nline-10"
    assert pio.tail_lines(text, 100) == text  # more lines requested than exist
    assert pio.tail_lines("", 5) == ""


def test_tail_lines_handles_trailing_newline() -> None:
    assert pio.tail_lines("a\nb\nc\n", 2) == "b\nc"


def test_pio_version_runs(monkeypatch: pytest.MonkeyPatch) -> None:
    """Happy path: `pio --version` exits 0 and prints a version string.

    This exercises subprocess spawn, timeout default, and the PioResult shape.
    Skipped if pio isn't installed (CI would need pio preinstalled).
    """
    try:
        result = pio.run(["--version"], timeout=30)
    except pio.PioError:
        pytest.skip("pio not available in this environment")
    assert result.returncode == 0
    assert "PlatformIO" in result.stdout or "platformio" in result.stdout.lower()
    assert result.duration_s > 0


def test_pio_bad_command_raises_pio_error() -> None:
    """`pio` returning non-zero must surface as PioError with stderr captured."""
    with pytest.raises(pio.PioError) as excinfo:
        pio.run(["this-subcommand-does-not-exist"], timeout=10)
    # PioError includes returncode + a tail of stderr/stdout.
    assert excinfo.value.returncode != 0


def test_pio_timeout_raises_pio_timeout(monkeypatch: pytest.MonkeyPatch) -> None:
    """Extremely short timeout on a command that takes longer must raise PioTimeout."""
    # `pio` startup alone typically takes ~200-500ms; a 1ms timeout reliably trips.
    with pytest.raises(pio.PioTimeout):
        pio.run(["--help"], timeout=0.001)


def test_run_json_parses_device_list() -> None:
    """`pio device list --json-output` is a known-valid JSON producer."""
    try:
        data = pio.run_json(["device", "list"], timeout=15)
    except pio.PioError:
        pytest.skip("pio not available in this environment")
    # Always a list; may be empty if nothing is plugged in.
    assert isinstance(data, list)
