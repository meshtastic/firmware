"""Mesh: traceroute from TX to RX round-trips with no intermediate hops.

TX sends a `TRACEROUTE_APP` request (RouteDiscovery with `want_response=True`)
addressed to RX's node_num. RX's firmware (`modules/TraceRouteModule.cpp`)
replies with a RouteDiscovery payload whose `route` / `route_back` lists
contain any intermediate relays and `snr_towards` / `snr_back` carry per-hop
SNRs. In a 2-device direct mesh there are no relays between TX and RX, so
both route lists must be empty and each SNR list carries exactly one entry
for the direct TX↔RX link.

Validates the full TRACEROUTE_APP portnum round-trip: request encoding, RX
firmware dispatch, RouteDiscovery payload construction, wire response, and
client-side decode through `meshtastic.__init__.py::protocols[TRACEROUTE_APP]`
(which is what publishes the `meshtastic.receive.traceroute` pubsub topic).
"""

from __future__ import annotations

import time
from typing import Any

import pytest
from meshtastic.mesh_interface import MeshInterface

from ._receive import ReceiveCollector, nudge_nodeinfo_port


@pytest.mark.timeout(240)
def test_traceroute_one_hop(mesh_pair: dict[str, Any]) -> None:
    """Runs for every directed pair. Asserts TX sends + RX responds, then
    inspects the captured RouteDiscovery to confirm the path is direct.

    Why the listener is on TX (not RX):
        The traceroute RESPONSE is addressed to TX (the original requester).
        The meshtastic Python client publishes `meshtastic.receive.traceroute`
        on the interface that received that response — which is TX's iface.
        A listener on RX would only see the inbound REQUEST, which lacks
        the SNR-towards / SNR-back fields the firmware only fills on reply.

    Why we ping RX's NodeInfo before sending:
        Traceroute requests are directed sends (wantResponse=True, specific
        destinationId) — subject to the same PKI_SEND_FAIL_PUBLIC_KEY trap
        as `test_direct_with_ack`. We open RX briefly to trigger the
        on-demand NodeInfo broadcast, then wait for TX's nodesByNum to
        populate RX's publicKey before calling sendTraceRoute.
    """
    tx_port = mesh_pair["tx"]["port"]
    rx_port = mesh_pair["rx"]["port"]
    rx_node_num = mesh_pair["rx"]["my_node_num"]
    tx_role = mesh_pair["tx_role"]
    rx_role = mesh_pair["rx_role"]
    assert rx_node_num is not None, f"{rx_role} my_node_num missing"

    with ReceiveCollector(
        tx_port, topic="meshtastic.receive.traceroute"
    ) as tx_listener:
        # Bilateral PKI warmup — traceroute requests are directed and
        # PKI-encrypted, so both sides need current pubkeys. See
        # `_receive.py::nudge_nodeinfo` and the test_direct_with_ack
        # comment for the full rationale (one-sided nudge lets err=35
        # PKI_UNKNOWN_PUBKEY slip through in whichever direction had
        # stale RX-side cache).
        nudge_nodeinfo_port(rx_port)  # RX via brief side-connection
        tx_listener.broadcast_nodeinfo_ping()  # TX via already-open iface

        # Poll TX's view of RX until the publicKey propagates. 45 s matches
        # the cap used in `test_direct_with_ack`; the re-nudge at 15 s
        # covers a LoRa collision on the first NodeInfo broadcast.
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

        # sendTraceRoute blocks internally on `waitForTraceRoute` and raises
        # `MeshInterface.MeshInterfaceError` on timeout. One retry covers a
        # transient LoRa collision on either the request or the reply.
        ok = False
        for _attempt in range(2):
            try:
                tx_listener._iface.sendTraceRoute(
                    dest=rx_node_num,
                    hopLimit=3,
                )
                ok = True
                break
            except MeshInterface.MeshInterfaceError:
                time.sleep(5.0)
        assert ok, (
            f"sendTraceRoute {tx_role}→{rx_role} timed out twice; the mesh "
            f"may be saturated or RX's TraceRouteModule is misrouting the "
            f"reply"
        )

        # sendTraceRoute already waited for the response internally, but
        # pubsub dispatch runs on the meshtastic-python reader thread —
        # give it a short grace window to queue the packet.
        packet = tx_listener.wait_for(
            lambda p: p.get("from") == rx_node_num,
            timeout=5.0,
        )
        assert packet is not None, (
            f"sendTraceRoute returned OK but no `receive.traceroute` packet "
            f"from RX (0x{rx_node_num:08x}) arrived via pubsub. Captured: "
            f"{tx_listener.snapshot()!r}"
        )

        # Inspect the decoded RouteDiscovery. The meshtastic client stores
        # the parsed protobuf (as a plain dict via MessageToDict) under
        # `decoded.traceroute` for this portnum; keys are camelCase because
        # protobuf JSON conversion uses `preserving_proto_field_name=False`
        # by default.
        decoded = packet.get("decoded", {})
        route_info = decoded.get("traceroute") or {}

        forward_hops = route_info.get("route") or []
        back_hops = route_info.get("routeBack") or []
        snr_towards = route_info.get("snrTowards") or []

        assert forward_hops == [], (
            f"traceroute forward `route` should be empty on a 2-device direct "
            f"mesh (no intermediaries between {tx_role} and {rx_role}); got "
            f"{forward_hops!r}"
        )
        assert back_hops == [], (
            f"traceroute `routeBack` should be empty on a 2-device direct "
            f"mesh; got {back_hops!r}"
        )
        # `snr_towards` has len(route) + 1 entries — one per hop plus a final
        # entry for the destination's receive SNR. Direct mesh → len(route)
        # is 0 → exactly 1 SNR entry.
        assert len(snr_towards) == 1, (
            f"traceroute `snrTowards` should carry exactly 1 entry (direct "
            f"link SNR) on a 2-device mesh; got {snr_towards!r}"
        )
