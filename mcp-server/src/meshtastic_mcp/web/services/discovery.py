"""Background USB discovery loop.

Polls the serial bus, reconciles each likely-Meshtastic device into the
registry (keyed by stable serial, surrogate key otherwise), flips vanished
devices offline, and broadcasts the deltas on ``device.update``.

Auto-enrichment: when a device is newly seen (or hops ports, or hasn't been
read yet), the loop fires a one-shot ``device_info`` in the background to sniff
its firmware version, hw_model → exact pio env, region, and node num — so those
populate on their own at plug-in, FleetLog-style. It is gated hard for safety:
skipped entirely while a test run holds the ports, serialized so only one
device is connected at a time, the device's live serial monitor is suspended
for the moment of the connect, pinned envs are never clobbered, and failures
back off instead of re-hammering every poll. Disable with
``MESHTASTIC_MCP_AUTO_ENRICH=0``.
"""

from __future__ import annotations

import asyncio
import logging
import os
import time

from meshtastic_mcp import devices as devices_lib

from ..db import repo_devices as rd
from . import identity

log = logging.getLogger("meshtastic_mcp.web.discovery")

POLL_SECONDS = 4.0
ENRICH_BACKOFF_S = 60.0  # after a failed connect, wait this long before retrying


class DeviceDiscovery:
    def __init__(self, db, hub, serialmon=None) -> None:
        self.db = db
        self.hub = hub
        self.serialmon = serialmon
        self.auto_enrich = os.environ.get("MESHTASTIC_MCP_AUTO_ENRICH", "1") != "0"
        self._task: asyncio.Task | None = None
        self._enrich_lock = asyncio.Lock()  # one device on the wire at a time
        self._enriched: dict[str, str] = {}  # serial -> port last enriched at
        self._failed: dict[str, float] = {}  # serial -> monotonic time to retry after
        self._enriching: set[str] = set()  # in-flight, to dedupe schedules

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
            changed = bool(row.pop("_is_new", False)) | bool(
                row.pop("_port_changed", False)
            )
            if changed:
                await self.hub.publish("device.update", row)
            self._maybe_enrich(row, changed)

        # Keep native nodes alive across scans — they aren't on the USB bus.
        natives = {
            d["serial_number"]
            for d in await rd.list_all(self.db)
            if d.get("kind") == "native"
        }
        newly_offline = await rd.mark_offline_except(self.db, seen | natives)
        for serial in newly_offline:
            self._enriched.pop(serial, None)  # re-verify if it comes back
            row = await rd.get(self.db, serial)
            if row:
                await self.hub.publish("device.update", row)

    # --- auto-enrichment --------------------------------------------------
    def _maybe_enrich(self, row: dict, changed: bool) -> None:
        """Decide whether a discovered device needs a background enrichment and,
        if so, schedule one. Cheap, synchronous gate; the actual connect happens
        in :meth:`_enrich`."""
        if not self.auto_enrich:
            return
        serial = row.get("serial_number")
        if not serial or row.get("kind") == "native":
            return
        # Lazy import avoids a module-load cycle (test_runner ← control ← ...).
        from . import test_runner

        if test_runner.is_running():
            return
        if serial in self._enriching:
            return
        retry_at = self._failed.get(serial)
        if retry_at is not None and time.monotonic() < retry_at:
            return
        # Enrich once per (serial, port). A completed-but-incomplete read sets a
        # backoff (handled in _enrich), so we never reconnect every poll.
        needs = changed or self._enriched.get(serial) != row.get("current_port")
        if needs:
            asyncio.create_task(self._enrich(serial))

    async def _enrich(self, serial: str) -> None:
        from meshtastic_mcp import info as mt_info

        from . import test_runner

        if serial in self._enriching:
            return
        self._enriching.add(serial)
        try:
            async with self._enrich_lock:  # serialize: one port open at a time
                if test_runner.is_running():
                    return
                row = await rd.get(self.db, serial)
                if (
                    row is None
                    or not row.get("online")
                    or row.get("kind") == "native"
                ):
                    return
                port = row.get("current_port")
                if not port:
                    return

                # Free the port from any live serial monitor for the connect.
                if self.serialmon is not None:
                    await self.serialmon.suspend(serial)
                try:
                    info = await asyncio.to_thread(mt_info.device_info, port)
                finally:
                    if self.serialmon is not None:
                        await self.serialmon.resume(serial)

                hw_model = info.get("hw_model")
                env = identity.env_for_hw_model(hw_model) if hw_model else None
                updated = await rd.update_enrichment(
                    self.db,
                    serial,
                    node_num=info.get("my_node_num"),
                    env=env,
                    hw_model=str(hw_model) if hw_model else None,
                    firmware_version=info.get("firmware_version"),
                    region=info.get("region"),
                )
                if info.get("firmware_version"):
                    # Full read — terminal for this port.
                    self._enriched[serial] = port
                    self._failed.pop(serial, None)
                    log.info(
                        "enriched %s: fw=%s hw=%s env=%s",
                        serial,
                        info.get("firmware_version"),
                        hw_model,
                        env,
                    )
                else:
                    # Connected but metadata wasn't ready yet — retry after backoff.
                    self._failed[serial] = time.monotonic() + ENRICH_BACKOFF_S
                if updated:
                    await self.hub.publish("device.update", updated)
        except Exception as exc:  # noqa: BLE001 - a flaky device shouldn't kill the loop
            self._failed[serial] = time.monotonic() + ENRICH_BACKOFF_S
            log.debug("enrichment of %s failed (backing off): %s", serial, exc)
        finally:
            self._enriching.discard(serial)

    async def _loop(self) -> None:
        while True:
            try:
                await self.scan_once()
            except asyncio.CancelledError:
                raise
            except Exception as exc:  # noqa: BLE001
                log.debug("discovery loop error: %s", exc)
            await asyncio.sleep(POLL_SECONDS)
