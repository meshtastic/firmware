"""In-memory registry of active serial monitor sessions and port locks.

Two things live here so the rest of the package has a single place to reach
them:
  1. `sessions`: `{session_id: SerialSession}` for pio device monitor subprocs.
  2. `port_locks`: `{port: threading.Lock}` so admin/info tools can fail fast
     when a serial monitor or another meshtastic client already owns a port.
"""

from __future__ import annotations

import threading
from typing import Any

from .serial_session import SerialSession, close_session

_LOCK = threading.Lock()
_sessions: dict[str, SerialSession] = {}
_port_locks: dict[str, threading.Lock] = {}


def register_session(session: SerialSession) -> None:
    with _LOCK:
        _sessions[session.id] = session


def get_session(session_id: str) -> SerialSession:
    with _LOCK:
        session = _sessions.get(session_id)
    if session is None:
        raise KeyError(f"Unknown session_id: {session_id!r}")
    return session


def remove_session(session_id: str) -> SerialSession | None:
    with _LOCK:
        return _sessions.pop(session_id, None)


def active_session_for_port(port: str) -> SerialSession | None:
    """Find any active (non-eof) session owning `port`."""
    sweep_dead()
    with _LOCK:
        for s in _sessions.values():
            if s.port == port and s.proc.poll() is None:
                return s
    return None


def all_sessions() -> list[SerialSession]:
    with _LOCK:
        return list(_sessions.values())


def sweep_dead() -> int:
    """Remove sessions whose subprocess has exited. Returns count removed."""
    removed_sessions: list[SerialSession] = []
    with _LOCK:
        for sid, s in list(_sessions.items()):
            if s.proc.poll() is not None:
                removed_sessions.append(_sessions.pop(sid))
    for session in removed_sessions:
        try:
            close_session(session)
        except Exception:
            pass
    return len(removed_sessions)


def shutdown_all() -> None:
    """Close every live session (called on server exit)."""
    with _LOCK:
        items = list(_sessions.items())
        _sessions.clear()
    for _sid, session in items:
        try:
            close_session(session)
        except Exception:
            pass


def port_lock(port: str) -> threading.Lock:
    """Per-port lock for SerialInterface / admin tool serialization."""
    with _LOCK:
        lock = _port_locks.get(port)
        if lock is None:
            lock = threading.Lock()
            _port_locks[port] = lock
        return lock


def snapshot() -> dict[str, Any]:
    """Debug dump: session count, port lock count."""
    with _LOCK:
        return {
            "sessions": len(_sessions),
            "port_locks": len(_port_locks),
        }
