"""Device-control safety gate.

The central invariant: no ``connect()``-based action (flash, reboot, config,
send-text, factory-reset, nodedb inject) may run while a test run holds the
ports. Every control endpoint funnels through ``_ensure_idle`` /
``_ensure_port_free`` first, which raise ``ControlBusy`` (surfaced as HTTP 409)
when the runner is active.
"""

from __future__ import annotations

from . import identity, test_runner


class ControlBusy(RuntimeError):
    """Raised when a control action is attempted while a test run is active."""


def env_for_device(d: dict) -> str | None:
    """The pio env to flash/bake for a device: its resolved env if it has one,
    otherwise the coarse role default."""
    return d.get("env") or identity.env_for_role(d.get("role"))


def _ensure_idle() -> None:
    if test_runner.is_running():
        raise ControlBusy("a test run is in progress")


def _ensure_port_free(port: str | None) -> None:
    # While a run is active it owns every port — nothing else may touch one.
    if test_runner.is_running():
        raise ControlBusy(f"port {port} is held by an active test run")
