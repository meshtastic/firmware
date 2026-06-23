"""Background USB discovery loop.

Polls the serial bus, reconciles each likely-Meshtastic device into the
registry (keyed by stable serial, surrogate key otherwise), flips vanished
devices offline, and broadcasts the deltas on ``device.update``. Enrichment
(connecting to read node_num/hw_model/env) is deliberately NOT done here — it
holds the port — it's triggered explicitly via the ``/refresh`` endpoint.
"""

from __future__ import annotations

import asyncio
import logging

from meshtastic_mcp import devices as devices_lib

from ..db import repo_devices as rd
from . import identity

log = logging.getLogger("meshtastic_mcp.web.discovery")

POLL_SECONDS = 4.0


class DeviceDiscovery:
    def __init__(self, db, hub) -> None:
        self.db = db
        self.hub = hub
        self._task: asyncio.Task | None = None

    def start(self) -> None:
        if self._task is None:
            self._task = asyncio.create_task(self._loop())

    async def stop(self) -> None:
        if self._task is not None:
            self._task.cancel()
            self._task = None

    async def scan_once(self) -> None:
        """One discovery pass. Runs the (blocking) enumeration in a thread."""
        try:
            found = await asyncio.to_thread(devices_lib.list_devices)
        except Exception as exc:  # noqa: BLE001
            log.debug("discovery enumeration failed: %s", exc)
            return

        seen: set[str] = set()
        for dev in found:
            if dev.get("blacklisted") or not dev.get("likely_meshtastic", False):
                continue
            key, _stable = identity.device_key(dev)
            role = identity.role_for_vid(dev.get("vid"))
            seen.add(key)
            row = await rd.upsert_from_discovery(
                self.db,
                serial_number=key,
                current_port=dev.get("port"),
                vid=dev.get("vid"),
                pid=dev.get("pid"),
                role=role,
            )
            if row.pop("_is_new", False) or row.pop("_port_changed", False):
                await self.hub.publish("device.update", row)

        # Keep native nodes alive across scans — they aren't on the USB bus.
        natives = {
            d["serial_number"]
            for d in await rd.list_all(self.db)
            if d.get("kind") == "native"
        }
        newly_offline = await rd.mark_offline_except(self.db, seen | natives)
        for serial in newly_offline:
            row = await rd.get(self.db, serial)
            if row:
                await self.hub.publish("device.update", row)

    async def _loop(self) -> None:
        while True:
            try:
                await self.scan_once()
            except asyncio.CancelledError:
                raise
            except Exception as exc:  # noqa: BLE001
                log.debug("discovery loop error: %s", exc)
            await asyncio.sleep(POLL_SECONDS)
