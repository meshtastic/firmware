"""Topic-based broadcast hub backing the single ``/ws`` socket.

Each connection subscribes to a set of topics; ``publish(topic, data)`` fans the
frame ``{"topic": ..., "data": ...}`` to every subscriber. Producers running off
the event loop (build threads, the pytest reader) use ``publish_threadsafe`` so
they don't touch asyncio primitives from the wrong thread.
"""

from __future__ import annotations

import asyncio
import logging
from typing import Any

log = logging.getLogger("meshtastic_mcp.web.ws")


class Connection:
    """One websocket peer and the topics it cares about. ``send`` is injected by
    the ``/ws`` handler so the hub stays transport-agnostic (and trivially
    unit-testable)."""

    def __init__(self, send) -> None:
        self.send = send
        self.topics: set[str] = set()


class Hub:
    def __init__(self) -> None:
        self._conns: set[Connection] = set()
        self._loop: asyncio.AbstractEventLoop | None = None

    def bind_loop(self, loop: asyncio.AbstractEventLoop) -> None:
        """Remember the event loop so ``publish_threadsafe`` can hop onto it."""
        self._loop = loop

    # --- connection lifecycle ---------------------------------------------
    def add(self, conn: Connection) -> None:
        self._conns.add(conn)

    def remove(self, conn: Connection) -> None:
        self._conns.discard(conn)

    def subscribe(self, conn: Connection, topic: str) -> None:
        conn.topics.add(topic)

    def unsubscribe(self, conn: Connection, topic: str) -> None:
        conn.topics.discard(topic)

    # --- publishing --------------------------------------------------------
    async def publish(self, topic: str, data: Any) -> None:
        if not self._conns:
            return
        frame = {"topic": topic, "data": data}
        dead: list[Connection] = []
        for conn in list(self._conns):
            if topic not in conn.topics:
                continue
            try:
                await conn.send(frame)
            except Exception:  # noqa: BLE001 - a dropped peer shouldn't kill a broadcast
                dead.append(conn)
        for conn in dead:
            self.remove(conn)

    def publish_threadsafe(self, topic: str, data: Any) -> None:
        """Schedule a publish from a non-event-loop thread."""
        if self._loop is None:
            return
        try:
            asyncio.run_coroutine_threadsafe(self.publish(topic, data), self._loop)
        except RuntimeError:
            log.debug("publish_threadsafe after loop close: %s", topic)
