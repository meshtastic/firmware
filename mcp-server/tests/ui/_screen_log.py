"""Parse `Screen: frame N/M name=X reason=Y` log lines from `_debug_log_buffer`.

The firmware emits one line per frame transition when
`USERPREFS_UI_TEST_LOG` is defined (see src/graphics/Screen.cpp). Tests use
these helpers to assert which frame is shown / to wait for a transition to
settle before taking a camera capture.
"""

from __future__ import annotations

import re
import time
from dataclasses import dataclass
from typing import Iterable, Iterator

FRAME_RE = re.compile(
    r"Screen: frame (?P<idx>\d+)/(?P<count>\d+) name=(?P<name>\S+) reason=(?P<reason>\S+)"
)


@dataclass(frozen=True)
class FrameEvent:
    idx: int
    count: int
    name: str
    reason: str
    raw: str

    @classmethod
    def parse(cls, line: str) -> "FrameEvent | None":
        m = FRAME_RE.search(line)
        if not m:
            return None
        return cls(
            idx=int(m["idx"]),
            count=int(m["count"]),
            name=m["name"],
            reason=m["reason"],
            raw=line,
        )


def iter_frame_events(lines: Iterable[str]) -> Iterator[FrameEvent]:
    for line in lines:
        evt = FrameEvent.parse(line)
        if evt is not None:
            yield evt


def get_current_frame(lines: list[str]) -> FrameEvent | None:
    """Return the most recent FrameEvent in `lines`, or None if none found."""
    for line in reversed(lines):
        evt = FrameEvent.parse(line)
        if evt is not None:
            return evt
    return None


def wait_for_frame(
    lines: list[str],
    expected_name: str,
    *,
    timeout_s: float = 5.0,
    poll_interval_s: float = 0.1,
    reason: str | None = None,
) -> FrameEvent:
    """Poll `lines` (the `_debug_log_buffer`) until a FrameEvent with
    `name=expected_name` appears after the call started. Raises TimeoutError
    with context if it doesn't arrive in `timeout_s`.

    `reason` optionally filters to events matching a specific cause
    (e.g. `"fn_f1"`, `"next"`, `"rebuild"`).
    """
    start_idx = len(lines)
    deadline = time.monotonic() + timeout_s
    last: FrameEvent | None = None
    while time.monotonic() < deadline:
        # Scan only lines appended since we started waiting.
        for line in lines[start_idx:]:
            evt = FrameEvent.parse(line)
            if evt is None:
                continue
            last = evt
            if evt.name == expected_name and (reason is None or evt.reason == reason):
                return evt
        time.sleep(poll_interval_s)

    seen = [e.name for e in iter_frame_events(lines[start_idx:])]
    raise TimeoutError(
        f"frame name={expected_name!r} reason={reason!r} not seen in {timeout_s}s; "
        f"saw {len(seen)} transition(s): {seen!r}; last={last!r}"
    )


def wait_for_any_frame(
    lines: list[str],
    *,
    timeout_s: float = 5.0,
    poll_interval_s: float = 0.1,
) -> FrameEvent:
    """Wait for ANY frame transition to appear after call-start. Useful for
    `no-op` tests that want to confirm a transition did NOT happen (via
    TimeoutError) vs. one that did.
    """
    start_idx = len(lines)
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        for line in lines[start_idx:]:
            evt = FrameEvent.parse(line)
            if evt is not None:
                return evt
        time.sleep(poll_interval_s)
    raise TimeoutError(f"no frame transition in {timeout_s}s")


def wait_for_reason(
    lines: list[str],
    reason: str,
    *,
    timeout_s: float = 5.0,
    poll_interval_s: float = 0.1,
) -> FrameEvent:
    """Wait for a frame event with `reason=<reason>` after call-start.

    Matches only on `reason` — useful when the caller knows *why* a
    transition should happen (e.g. `fn_f1`, `rebuild`) but not which named
    frame the firmware will land on for this particular board.
    """
    start_idx = len(lines)
    deadline = time.monotonic() + timeout_s
    last: FrameEvent | None = None
    while time.monotonic() < deadline:
        for line in lines[start_idx:]:
            evt = FrameEvent.parse(line)
            if evt is None:
                continue
            last = evt
            if evt.reason == reason:
                return evt
        time.sleep(poll_interval_s)
    raise TimeoutError(
        f"no frame with reason={reason!r} in {timeout_s}s; last={last!r}"
    )


def assert_no_frame_change(
    lines: list[str],
    *,
    wait_s: float = 2.0,
) -> None:
    """Assert that NO new FrameEvent lines arrive within `wait_s`.

    Used by idempotency / no-op tests (e.g. BACK on home frame).
    """
    start_idx = len(lines)
    time.sleep(wait_s)
    new = [
        e for e in (FrameEvent.parse(ln) for ln in lines[start_idx:]) if e is not None
    ]
    if new:
        raise AssertionError(
            f"expected no frame change in {wait_s}s, but saw {len(new)} event(s): "
            f"{[(e.reason, e.name) for e in new]!r}"
        )


__all__ = [
    "FRAME_RE",
    "FrameEvent",
    "assert_no_frame_change",
    "get_current_frame",
    "iter_frame_events",
    "wait_for_any_frame",
    "wait_for_frame",
    "wait_for_reason",
]
