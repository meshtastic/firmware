"""Pin the `Screen: frame N/M name=X reason=Y` regex + FrameEvent dataclass.

The firmware-side format lives in `src/graphics/Screen.cpp::logFrameChange`;
if the format string changes, this test — and the parser in
`tests/ui/_screen_log.py` — have to be updated together.
"""

from __future__ import annotations

from tests.ui._screen_log import FRAME_RE, FrameEvent, iter_frame_events


class TestFrameEventParse:
    def test_exact_firmware_output(self) -> None:
        raw = "Screen: frame 2/8 name=home reason=next"
        evt = FrameEvent.parse(raw)
        assert evt is not None
        assert evt.idx == 2
        assert evt.count == 8
        assert evt.name == "home"
        assert evt.reason == "next"
        assert evt.raw == raw

    def test_with_log_prefix(self) -> None:
        """Log lines may be preamble-wrapped by the firmware LOG_INFO macro
        (timestamp, severity, etc.) — the regex uses .search() not .match()
        so prefixes are fine."""
        raw = "[INFO] 00:12:34  567   Screen: frame 4/12 name=nodelist_nodes reason=fn_f3    "
        evt = FrameEvent.parse(raw)
        assert evt is not None
        assert evt.idx == 4
        assert evt.count == 12
        assert evt.name == "nodelist_nodes"
        assert evt.reason == "fn_f3"

    def test_rebuild_reason(self) -> None:
        evt = FrameEvent.parse("Screen: frame 0/5 name=deviceFocused reason=rebuild")
        assert evt is not None
        assert evt.reason == "rebuild"

    def test_all_fn_reasons(self) -> None:
        for k in range(1, 6):
            evt = FrameEvent.parse(
                f"Screen: frame {k - 1}/8 name=settings reason=fn_f{k}"
            )
            assert evt is not None and evt.reason == f"fn_f{k}"

    def test_unknown_name_is_preserved(self) -> None:
        """If the reverse-map returns 'unknown', that still parses cleanly."""
        evt = FrameEvent.parse("Screen: frame 99/100 name=unknown reason=prev")
        assert evt is not None and evt.name == "unknown"

    def test_non_matching_line_returns_none(self) -> None:
        assert FrameEvent.parse("BOOT Booting firmware 2.7.23") is None
        assert FrameEvent.parse("") is None
        assert FrameEvent.parse("Screen: without the right format") is None


class TestIterFrameEvents:
    def test_filters_non_matching_lines(self) -> None:
        lines = [
            "Booting...",
            "Screen: frame 1/5 name=home reason=rebuild",
            "Some other log line",
            "Screen: frame 2/5 name=textMessage reason=next",
        ]
        evts = list(iter_frame_events(lines))
        assert len(evts) == 2
        assert evts[0].reason == "rebuild"
        assert evts[1].reason == "next"


class TestRegexAnchoring:
    def test_regex_is_compiled(self) -> None:
        assert FRAME_RE.search("Screen: frame 0/0 name=home reason=next") is not None

    def test_regex_allows_unusual_names(self) -> None:
        r"""Name is `\S+`, so compound names with underscores/digits match."""
        m = FRAME_RE.search("Screen: frame 5/10 name=nodelist_hopsignal reason=fn_f2")
        assert m is not None and m["name"] == "nodelist_hopsignal"
