"""Generate an initial set of MeshPackets.

The fuzzer uses these MeshPackets as an initial seed of test candidates.

It's also good to add any previously discovered crash test cases to this list
to avoid future regressions.

If left unset, the following values will be automatically set by the fuzzer.
  - to: automatically set to the running node's NodeID
  - from: automatically set to the running node's NodeID
  - id: automatically set to the value of an incrementing counter

Additionally, if `pki_encrypted` is populated in the packet, the first admin key
will be copied into the `public_key` field.
"""

import base64

from meshtastic import BROADCAST_NUM
from meshtastic.protobuf import (
    admin_pb2,
    atak_pb2,
    mesh_pb2,
    portnums_pb2,
    telemetry_pb2,
)


def From(node: int = 9):
    """Return a dict suitable for **kwargs for populating the 'from' field.

    'from' is a reserved keyword in Python. It can't be used directly as an
    argument to the MeshPacket constructor. Rather **From() can be used as
    the final argument to provide the from node as a **kwarg.

    Defaults to 9 if no value is provided.
    """
    return {"from": node}


packets = (
    (
        "position",
        mesh_pb2.MeshPacket(
            decoded=mesh_pb2.Data(
                portnum=portnums_pb2.PortNum.POSITION_APP,
                payload=mesh_pb2.Position(
                    latitude_i=int(1 * 1e7),
                    longitude_i=int(2 * 1e7),
                    altitude=5,
                    precision_bits=32,
                ).SerializeToString(),
            ),
            to=BROADCAST_NUM,
            **From(),
        ),
    ),
    (
        "telemetry",
        mesh_pb2.MeshPacket(
            decoded=mesh_pb2.Data(
                portnum=portnums_pb2.PortNum.TELEMETRY_APP,
                payload=telemetry_pb2.Telemetry(
                    time=1736192207,
                    device_metrics=telemetry_pb2.DeviceMetrics(
                        battery_level=101,
                        channel_utilization=8,
                        air_util_tx=2,
                        uptime_seconds=42,
                    ),
                ).SerializeToString(),
            ),
            to=BROADCAST_NUM,
            **From(),
        ),
    ),
    (
        "text",
        mesh_pb2.MeshPacket(
            decoded=mesh_pb2.Data(
                portnum=portnums_pb2.PortNum.TEXT_MESSAGE_APP,
                payload=b"Hello world",
            ),
            to=BROADCAST_NUM,
            **From(),
        ),
    ),
    (
        "user",
        mesh_pb2.MeshPacket(
            decoded=mesh_pb2.Data(
                portnum=portnums_pb2.PortNum.NODEINFO_APP,
                payload=mesh_pb2.User(
                    id="!00000009",
                    long_name="Node 9",
                    short_name="N9",
                    macaddr=b"\x00\x00\x00\x00\x00\x09",
                    hw_model=mesh_pb2.HardwareModel.RAK4631,
                    public_key=base64.b64decode(
                        "L0ih/6F41itofdE8mYyHk1SdfOJ/QRM1KQ+pO4vEEjQ="
                    ),
                ).SerializeToString(),
            ),
            **From(),
        ),
    ),
    (
        "traceroute",
        mesh_pb2.MeshPacket(
            decoded=mesh_pb2.Data(
                portnum=portnums_pb2.PortNum.TRACEROUTE_APP,
                payload=mesh_pb2.RouteDiscovery(
                    route=[10],
                ).SerializeToString(),
            ),
            **From(),
        ),
    ),
    (
        "routing",
        mesh_pb2.MeshPacket(
            decoded=mesh_pb2.Data(
                portnum=portnums_pb2.PortNum.ROUTING_APP,
                payload=mesh_pb2.Routing(
                    error_reason=mesh_pb2.Routing.NO_RESPONSE,
                ).SerializeToString(),
            ),
            **From(),
        ),
    ),
    (
        "admin",
        mesh_pb2.MeshPacket(
            decoded=mesh_pb2.Data(
                portnum=portnums_pb2.PortNum.ADMIN_APP,
                payload=admin_pb2.AdminMessage(
                    get_owner_request=True,
                ).SerializeToString(),
            ),
            pki_encrypted=True,
            **From(),
        ),
    ),
    (
        "atak",
        mesh_pb2.MeshPacket(
            decoded=mesh_pb2.Data(
                portnum=portnums_pb2.PortNum.ATAK_PLUGIN,
                payload=atak_pb2.TAKPacket(
                    is_compressed=True,
                    # Note, the strings are not valid for a compressed message, but will
                    # give the fuzzer a starting point.
                    contact=atak_pb2.Contact(
                        callsign="callsign", device_callsign="device_callsign"
                    ),
                    chat=atak_pb2.GeoChat(
                        message="message", to="to", to_callsign="to_callsign"
                    ),
                ).SerializeToString(),
            ),
            **From(),
        ),
    ),
)

for name, packet in packets:
    with open(f"{name}.MeshPacket", "wb") as f:
        f.write(packet.SerializeToString())
