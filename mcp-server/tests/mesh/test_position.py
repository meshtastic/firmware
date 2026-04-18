"""Mesh: on-demand Position request gets a prompt reply.

Exercises ``POSITION_APP`` (portnum 3) — the core Meshtastic portnum that
map apps and fleet dashboards depend on. Pattern mirrors
``test_telemetry_request_reply``: TX sends a directed Position request
with ``want_response=True``; RX's ``modules/PositionModule.cpp::allocReply``
fires and returns a populated Position packet.

Why we seed RX with a fake fixed position first:
``PositionModule::allocPositionPacket`` returns nullptr when
``localPosition.latitude_i == 0 && longitude_i == 0``
(`src/modules/PositionModule.cpp:201-204`). Our test devices have no
GPS and start life at (0, 0) — without a seed, RX can't build a reply
and the firmware NAKs with ``Routing.Error.NO_RESPONSE`` instead. We
use ``localNode.setFixedPosition(lat, lon, alt)`` — an AdminMessage
handled on the local device — to stash a non-zero position in NVS
before the request, then ``removeFixedPosition`` at teardown so
downstream tests don't inherit it.

Why request/reply instead of a periodic broadcast test:
  * Request/reply runs in seconds on a direct 2-device mesh instead of
    minutes waiting for the next broadcast window.
  * Matches the real ``meshtastic --request-position`` CLI path, so
    we're exercising user-facing behavior end-to-end.

Matching the reply via ``onResponse``: the library stores
``responseHandlers[sent_packet.id]`` at send time and fires when a
received packet's ``decoded.request_id`` matches. This rejects stray
broadcasts + stale replies to prior requests.
"""

from __future__ import annotations

import threading
import time
from typing import Any

import pytest
from meshtastic.protobuf import mesh_pb2, portnums_pb2  # type: ignore[import-untyped]
from meshtastic_mcp.connection import connect

from ._receive import ReceiveCollector, nudge_nodeinfo_port

# Fake position — Null Island plus an offset so lat/lon are both non-zero
# (otherwise `allocPositionPacket` bails out per the firmware rationale
# in the module docstring). The specific coordinates don't matter; the
# test validates the *routing + decode* path, not GPS accuracy. Pick
# an alt value of 10 (meters) to match proto's uint32 for the field.
_FAKE_LAT_DEG = 37.7749  # approx San Francisco, non-zero, not default
_FAKE_LON_DEG = -122.4194
_FAKE_ALT_M = 10


def _seed_fixed_position(port: str) -> None:
    """Stash a non-zero fixed position in the RX device's local storage
    so its ``PositionModule::allocPositionPacket`` returns a reply
    instead of nullptr."""
    with connect(port=port) as iface:
        iface.localNode.setFixedPosition(_FAKE_LAT_DEG, _FAKE_LON_DEG, _FAKE_ALT_M)


def _clear_fixed_position(port: str) -> None:
    """Undo :func:`_seed_fixed_position` so downstream tests see clean
    state. Best-effort — exceptions here would mask the real test
    assertion, so we swallow them."""
    try:
        with connect(port=port) as iface:
            iface.localNode.removeFixedPosition()
    except Exception:
        pass


@pytest.mark.timeout(180)
def test_position_request_reply(mesh_pair: dict[str, Any]) -> None:
    """Runs for every directed pair. TX asks RX for its current Position
    via ``want_response=True``; asserts the reply carries a decoded
    Position payload.
    """
    tx_port = mesh_pair["tx"]["port"]
    rx_port = mesh_pair["rx"]["port"]
    rx_node_num = mesh_pair["rx"]["my_node_num"]
    tx_role = mesh_pair["tx_role"]
    rx_role = mesh_pair["rx_role"]
    assert rx_node_num is not None, f"{rx_role} my_node_num missing"

    # Seed RX with a fake fixed position before the request. Without
    # this, `allocPositionPacket` returns nullptr (zero lat/lon) and
    # the firmware NAKs our request with Routing.Error.NO_RESPONSE.
    # Runs BEFORE opening tx_listener so the brief RX connection here
    # doesn't race the listener's port acquisition.
    _seed_fixed_position(rx_port)
    try:
        # Topic is irrelevant — we match via onResponse, not pubsub — but
        # keeping a concrete subscription avoids receiving every packet type.
        with ReceiveCollector(
            tx_port, topic="meshtastic.receive.position"
        ) as tx_listener:
            # Bilateral PKI warmup — directed requests are encrypted with
            # RX's pubkey; RX needs TX's pubkey to decrypt. Same pattern as
            # test_telemetry_request_reply / test_direct_with_ack / test_traceroute.
            nudge_nodeinfo_port(rx_port)
            tx_listener.broadcast_nodeinfo_ping()

            pk_deadline = time.monotonic() + 45.0
            last_nudge = time.monotonic()
            last_rec: dict[str, Any] = {}
            while time.monotonic() < pk_deadline:
                last_rec = (tx_listener._iface.nodesByNum or {}).get(rx_node_num, {})
                if last_rec.get("user", {}).get("publicKey"):
                    break
                if time.monotonic() - last_nudge > 15.0:
                    nudge_nodeinfo_port(rx_port)
                    tx_listener.broadcast_nodeinfo_ping()
                    last_nudge = time.monotonic()
                time.sleep(1.0)
            else:
                pytest.fail(
                    f"TX ({tx_role}) never saw RX ({rx_role}) public key within "
                    f"45s; nodesByNum entry={last_rec!r}"
                )

            # Send the request. An empty Position payload + wantResponse=True
            # is the firmware's cue to reply via `allocPositionPacket()`
            # with the current local position. No oneof variant to set —
            # Position is a flat message (unlike Telemetry), so the
            # default-constructed empty body is the canonical request.
            #
            # One retry for transient LoRa collisions on request or reply.
            reply_holder: list[dict[str, Any]] = []
            got_reply = threading.Event()

            def _on_reply(packet: dict[str, Any]) -> None:
                reply_holder.append(packet)
                got_reply.set()

            got = None
            for _attempt in range(2):
                got_reply.clear()
                del reply_holder[:]
                req = mesh_pb2.Position()
                tx_listener._iface.sendData(
                    req,
                    destinationId=rx_node_num,
                    portNum=portnums_pb2.PortNum.POSITION_APP,
                    wantResponse=True,
                    onResponse=_on_reply,
                    hopLimit=3,
                )
                if got_reply.wait(timeout=45.0):
                    got = reply_holder[0]
                    break
                time.sleep(5.0)

            assert got is not None, (
                f"no Position reply from {rx_role} (0x{rx_node_num:08x}) "
                f"within 90s of 2 requests; onResponse callback never "
                f"fired. PositionModule::allocReply may be throttled "
                f"(3-min per-peer window) — check firmware log for "
                f"'Skip Position reply'."
            )

            # Sanity: reply origin matches. Same rejection criterion as
            # the telemetry test — protects against a firmware routing
            # bug that dispatches our onResponse on the wrong packet.
            assert got.get("from") == rx_node_num, (
                f"Position reply origin mismatch: "
                f"from=0x{got.get('from'):08x}, "
                f"expected 0x{rx_node_num:08x}"
            )

            # Validate the decode path. `decoded.position` is the
            # MessageToDict version of the Position proto; seeding with
            # a fake fixed position ensures lat/lon are non-zero and
            # the proto strip-defaults behavior keeps them in the dict.
            decoded = got.get("decoded", {})
            assert "position" in decoded, (
                f"Position reply from {rx_role} had no `decoded.position` — "
                f"POSITION_APP decode failed. decoded={decoded!r}"
            )
    finally:
        _clear_fixed_position(rx_port)
