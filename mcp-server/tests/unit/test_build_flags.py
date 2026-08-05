"""Unit tests for the `build_flags` injection on `flash.build()`.

We don't actually run pio here — too slow, requires hardware-aware envs.
We test the translation layer (`_build_flags_env`) and that the env vars
are threaded through pio.run correctly via mock.
"""

from __future__ import annotations

from unittest.mock import patch

from meshtastic_mcp import flash, pio


class TestBuildFlagsEnv:
    def test_simple_value(self) -> None:
        out = flash._build_flags_env({"DEBUG_HEAP": 1})
        assert out == {"PLATFORMIO_BUILD_FLAGS": "-DDEBUG_HEAP=1"}

    def test_string_value(self) -> None:
        out = flash._build_flags_env({"FOO": "bar"})
        assert out == {"PLATFORMIO_BUILD_FLAGS": "-DFOO=bar"}

    def test_bool_true_is_bare_flag(self) -> None:
        out = flash._build_flags_env({"DEBUG_HEAP": True})
        assert out == {"PLATFORMIO_BUILD_FLAGS": "-DDEBUG_HEAP"}

    def test_bool_false_dropped(self) -> None:
        out = flash._build_flags_env({"DEBUG_HEAP": False, "OTHER": 1})
        assert out == {"PLATFORMIO_BUILD_FLAGS": "-DOTHER=1"}

    def test_none_dropped(self) -> None:
        out = flash._build_flags_env({"DEBUG_HEAP": None})
        assert out == {}

    def test_multiple_combined(self) -> None:
        out = flash._build_flags_env({"DEBUG_HEAP": 1, "FOO": "x", "BAR": True})
        # Order isn't guaranteed in dict iteration, so check membership.
        flags = out["PLATFORMIO_BUILD_FLAGS"].split()
        assert set(flags) == {"-DDEBUG_HEAP=1", "-DFOO=x", "-DBAR"}


class TestBuildPropagatesFlags:
    def test_extra_env_passed_to_pio_run(self) -> None:
        # Mock pio.run so we don't actually invoke pio. Capture extra_env.
        captured = {}

        class _StubResult:
            returncode = 0
            stdout = ""
            stderr = ""
            duration_s = 0.1

        def _stub(args, **kwargs):
            captured["args"] = args
            captured["kwargs"] = kwargs
            return _StubResult()

        with patch.object(pio, "run", side_effect=_stub):
            with patch.object(flash, "_artifacts_for", return_value=[]):
                out = flash.build(
                    "fake-env",
                    with_manifest=False,
                    build_flags={"DEBUG_HEAP": 1},
                )
        assert captured["args"] == ["run", "-e", "fake-env"]
        assert captured["kwargs"]["extra_env"] == {
            "PLATFORMIO_BUILD_FLAGS": "-DDEBUG_HEAP=1"
        }
        assert out["build_flags"] == {"DEBUG_HEAP": 1}

    def test_no_flags_means_no_extra_env(self) -> None:
        captured = {}

        class _StubResult:
            returncode = 0
            stdout = ""
            stderr = ""
            duration_s = 0.1

        def _stub(args, **kwargs):
            captured["kwargs"] = kwargs
            return _StubResult()

        with patch.object(pio, "run", side_effect=_stub):
            with patch.object(flash, "_artifacts_for", return_value=[]):
                flash.build("fake-env", with_manifest=False)
        assert captured["kwargs"]["extra_env"] is None
