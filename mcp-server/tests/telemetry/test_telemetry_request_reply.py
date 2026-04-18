"""Telemetry: on-demand device-metrics request gets a prompt reply.

Complementary to ``test_device_telemetry_broadcast`` — that one witnesses the
firmware's *periodic* broadcast (900 s default interval, up to ~7 min worst
case). This one exercises the *request/reply* path: TX sends a
``meshtastic_Telemetry`` with the ``device_metrics`` variant-tag set and
``want_response=True`` on ``TELEMETRY_APP`` to RX, and RX's
``modules/Telemetry/DeviceTelemetry.cpp::allocReply`` fires immediately with
populated ``DeviceMetrics``. On a direct 2-device mesh the whole round-trip
finishes in under a minute even from a cold boot.

Validates:
  * ``sendData(portNum=TELEMETRY_APP, want_response=True)`` encodes + routes
    to RX (directed, PKI-encrypted to RX's pubkey)
  * RX's ``DeviceTelemetryModule::handleReceivedProtobuf`` dispatches to
    ``allocReply`` — which is only invoked by the framework when
    ``want_response`` is set on the incoming packet
  * The reply carries a ``DeviceMetrics`` sub-message with at least one
    non-zero field (uptime_seconds is guaranteed non-zero a few seconds
    after boot, so it reliably survives protobuf's default-value
    serialization stripping)
  * The reply routes back to TX and gets matched against the original
    request via ``request_id`` — using the library's ``onResponse``
    callback mechanism, which stores the handler at
    ``responseHandlers[sent_packet.id]`` and dispatches when a packet
    arrives with ``decoded.request_id == sent_packet.id``. This is more
    precise than a pubsub ``from==rx_node_num`` filter, which can
    accidentally match RX's periodic broadcast or a stale reply to a
    different prior request.
"""

from __future__ import annotations

import threading
import time
from typing import Any

import pytest
from meshtastic.protobuf import (  # type: ignore[import-untyped]
    portnums_pb2,
    telemetry_pb2,
)

from ..mesh._receive import ReceiveCollector, nudge_nodeinfo_port

# Fields on the DeviceMetrics sub-message. The camelCase versions are what
# `google.protobuf.json_format.MessageToDict` emits (preserving_proto_field_name
# defaults to False); the snake_case names are the proto-source spellings.
_DEVICE_METRICS_FIELDS = (
    "batteryLevel",
    "voltage",
    "channelUtilization",
    "airUtilTx",
    "uptimeSeconds",
)


@pytest.mark.timeout(240)
def test_telemetry_request_reply(mesh_pair: dict[str, Any]) -> None:
    """Runs for every directed pair. TX requests RX's telemetry via
    ``want_response=True`` and asserts the reply arrives with populated
    DeviceMetrics.
    """
    tx_port = mesh_pair["tx"]["port"]
    rx_port = mesh_pair["rx"]["port"]
    rx_node_num = mesh_pair["rx"]["my_node_num"]
    tx_role = mesh_pair["tx_role"]
    rx_role = mesh_pair["rx_role"]
    assert rx_node_num is not None, f"{rx_role} my_node_num missing"

    # ReceiveCollector is still used to hold TX's SerialInterface open and
    # give us `tx_listener._iface` for sendData / nodesByNum polling. The
    # subscribed topic is irrelevant for this test (we match via
    # onResponse, not pubsub), but keeping a concrete topic avoids the
    # surprise of a pubsub wildcard receiving every packet type.
    with ReceiveCollector(tx_port, topic="meshtastic.receive.telemetry") as tx_listener:
        # Bilateral PKI warmup — nudge BOTH sides to rebroadcast their
        # NodeInfo (with current pubkey) before the directed send.
        #  * Nudging only RX gets RX's key → TX, but leaves RX with a
        #    potentially stale TX pubkey → RX NAKs our request with
        #    err=35 (PKI_UNKNOWN_PUBKEY) and we see no reply.
        #  * Nudging only TX is the mirror failure.
        # See `tests/mesh/_receive.py::nudge_nodeinfo` for firmware path.
        nudge_nodeinfo_port(rx_port)  # briefly opens RX to send heartbeat
        tx_listener.broadcast_nodeinfo_ping()  # TX via the already-open iface

        pk_deadline = time.monotonic() + 45.0
        last_nudge = time.monotonic()
        last_rec: dict[str, Any] = {}
        while time.monotonic() < pk_deadline:
            last_rec = (tx_listener._iface.nodesByNum or {}).get(rx_node_num, {})
            if last_rec.get("user", {}).get("publicKey"):
                break
            if time.monotonic() - last_nudge > 15.0:
                # Re-nudge both sides — LoRa collisions can drop either
                # direction's NodeInfo broadcast independently.
                nudge_nodeinfo_port(rx_port)
                tx_listener.broadcast_nodeinfo_ping()
                last_nudge = time.monotonic()
            time.sleep(1.0)
        else:
            pytest.fail(
                f"TX ({tx_role}) never saw RX ({rx_role}) public key within "
                f"45s; nodesByNum entry={last_rec!r}"
            )

        # Send the request. The Telemetry protobuf has a `which_variant`
        # oneof tag that the firmware uses to decide which reply to build
        # (see `src/modules/Telemetry/DeviceTelemetry.cpp::allocReply`):
        #   device_metrics_tag → getDeviceTelemetry()
        #   local_stats_tag    → getLocalStatsTelemetry()
        #   anything else      → return NULL (request silently dropped)
        # An empty `Telemetry()` has `which_variant = UNSET (0)`, so we MUST
        # explicitly set the variant. `CopyFrom(DeviceMetrics())` with a
        # default-constructed sub-message is the canonical Python-protobuf
        # idiom for "set the oneof tag without populating fields" — matching
        # how `MeshInterface.sendTelemetry()` constructs requests for the
        # other variants.
        #
        # Matching the reply: the meshtastic client's `onResponse` callback
        # mechanism fires ONLY for packets whose `decoded.request_id` equals
        # the original outgoing packet's `id`. That's exactly the semantic
        # we want — rejects periodic broadcasts (no request_id), rejects
        # stale replies to prior requests (different request_id), and
        # tolerates the firmware's reply_id/request_id naming quirk
        # (firmware's `setReplyTo` writes the original packet's id into
        # `decoded.request_id`, not `decoded.reply_id`).
        #
        # One retry covers transient LoRa collisions on request or reply.
        reply_holder: list[dict[str, Any]] = []
        got_reply = threading.Event()

        def _on_reply(packet: dict[str, Any]) -> None:
            reply_holder.append(packet)
            got_reply.set()

        got = None
        for _attempt in range(2):
            got_reply.clear()
            del reply_holder[:]
            req = telemetry_pb2.Telemetry()
            req.device_metrics.CopyFrom(telemetry_pb2.DeviceMetrics())
            tx_listener._iface.sendData(
                req,
                destinationId=rx_node_num,
                portNum=portnums_pb2.PortNum.TELEMETRY_APP,
                wantResponse=True,
                onResponse=_on_reply,
                hopLimit=3,
            )
            if got_reply.wait(timeout=45.0):
                got = reply_holder[0]
                break
            time.sleep(5.0)

        assert got is not None, (
            f"no telemetry reply from {rx_role} (0x{rx_node_num:08x}) within "
            f"90s of 2 requests; onResponse callback never fired. Captured "
            f"{len(tx_listener.snapshot())} unrelated telemetry packet(s): "
            f"{[hex(p.get('from') or 0) for p in tx_listener.snapshot()]!r}"
        )

        # Sanity: the reply's origin matches — a firmware bug that routed
        # the response to the wrong sender would make onResponse fire on
        # the wrong packet.
        assert got.get("from") == rx_node_num, (
            f"telemetry reply origin mismatch: from=0x{got.get('from'):08x}, "
            f"expected 0x{rx_node_num:08x}"
        )

        # Inspect the decoded Telemetry payload. The meshtastic client stores
        # it under `decoded.telemetry`; DeviceMetrics under `.deviceMetrics`.
        decoded = got.get("decoded", {})
        telem = decoded.get("telemetry") or {}
        dm = telem.get("deviceMetrics") or {}

        # A populated reply must contain at least one DeviceMetrics field.
        # Protobuf's JSON serializer strips default-valued (zero) fields,
        # so a bare `deviceMetrics: {}` would mean the firmware wrote the
        # sub-message but every field was zero — plausible right at boot
        # but not for a device that's been running long enough for a test
        # session's warmup + NodeInfo exchange (~10-30 s uptime minimum).
        populated = [k for k in _DEVICE_METRICS_FIELDS if k in dm]
        assert populated, (
            f"telemetry reply from {rx_role} carried no DeviceMetrics fields; "
            f"decoded.telemetry={telem!r}"
        )
