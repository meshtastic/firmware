"""Mesh: a fixed-position broadcast from TX arrives decoded on RX.

Exercises ``POSITION_APP`` (portnum 3) — the core Meshtastic portnum that
map apps and fleet dashboards depend on. Without real GPS hardware on
either device we put TX into **fixed-position** mode with a short
broadcast interval; the firmware's ``PositionModule::runOnce`` then
emits a Position packet every N seconds independent of GPS state. RX
listens on ``meshtastic.receive.position`` and verifies the decode.

Scope: validating the *routing + decode* path through
``modules/PositionModule.cpp``, not GPS accuracy. We don't set or assert
any specific lat/lon — the test is "a Position protobuf rode the wire
and was decoded correctly", which is what a downstream map client needs.

Why directed/PKI warmup matters: the config writes (``set_config``) are
directed admin sends. They don't fire unless the firmware trusts the
sender — same PKI staleness trap as the other directed-send tests, so
we do the bilateral warmup first.
"""

from __future__ import annotations

import time
from typing import Any

import pytest
from meshtastic_mcp import admin

from ._receive import ReceiveCollector, nudge_nodeinfo_port


def _restore_position_config(port: str, orig: dict[str, Any]) -> None:
    """Best-effort teardown: restore the 3 config fields this test mutates.

    Runs in a ``finally`` block — if the test failed mid-way we still
    want the device back to its pre-test state so downstream tests
    don't inherit a short broadcast interval + fixed-position flag.
    Swallows exceptions to avoid masking the underlying assertion.
    """
    for path, value in (
        ("position.fixed_position", orig.get("fixed_position", False)),
        (
            "position.position_broadcast_secs",
            orig.get("position_broadcast_secs", 0),
        ),
        (
            "position.position_broadcast_smart_enabled",
            orig.get("position_broadcast_smart_enabled", True),
        ),
    ):
        try:
            admin.set_config(path, value, port=port)
        except Exception:
            pass


@pytest.mark.timeout(240)
def test_position_broadcast_and_receive(mesh_pair: dict[str, Any]) -> None:
    """Runs for every directed pair. TX emits a periodic Position; RX
    receives and decodes it via the ``receive.position`` pubsub topic.
    """
    tx_port = mesh_pair["tx"]["port"]
    rx_port = mesh_pair["rx"]["port"]
    tx_node_num = mesh_pair["tx"]["my_node_num"]
    tx_role = mesh_pair["tx_role"]
    rx_role = mesh_pair["rx_role"]
    assert tx_node_num is not None, f"{tx_role} my_node_num missing"

    # Snapshot current position config so teardown can restore.
    orig_position = (
        admin.get_config("position", port=tx_port).get("config", {}).get("position", {})
    )

    try:
        # Configure TX for periodic fixed-position broadcasts. 30 s is a
        # compromise: short enough for the test to finish inside the
        # 240 s timeout, long enough that one dropped broadcast still
        # leaves a 90 s window for the next. smart_enabled=False
        # disables the distance/interval throttle in
        # `PositionModule::hasChanged` so we get broadcasts on a fixed
        # cadence instead of "only when position changes".
        admin.set_config("position.fixed_position", True, port=tx_port)
        admin.set_config("position.position_broadcast_secs", 30, port=tx_port)
        admin.set_config(
            "position.position_broadcast_smart_enabled", False, port=tx_port
        )
        # Small settle: the firmware applies config writes asynchronously,
        # and the next broadcast is scheduled off the *new* interval. A
        # brief pause keeps a racy first-broadcast-at-the-old-interval
        # from confusing the listener.
        time.sleep(2.0)

        # Bilateral PKI warmup — broadcast packets aren't PKI-encrypted
        # (they use channel keys), but we also want both sides in each
        # other's node tables so `PositionModule::hasChanged` doesn't
        # suppress the broadcast as "no peers to tell". NodeInfo pings
        # achieve both.
        nudge_nodeinfo_port(tx_port)
        nudge_nodeinfo_port(rx_port)
        time.sleep(3.0)

        with ReceiveCollector(rx_port, topic="meshtastic.receive.position") as rx:
            got = rx.wait_for(
                lambda pkt: pkt.get("from") == tx_node_num,
                timeout=120.0,  # 2× the broadcast_secs for one retransmit
            )

        assert got is not None, (
            f"no Position packet from {tx_role} (0x{tx_node_num:08x}) within "
            f"120 s; RX ({rx_role}) saw {len(rx.snapshot())} position packet(s) "
            f"from {[hex(p.get('from') or 0) for p in rx.snapshot()]!r}. "
            f"Check that TX's PositionModule is enabled and the broadcast "
            f"schedule isn't throttled elsewhere."
        )

        # Validate the decode path. `decoded.position` is the
        # MessageToDict version of the Position proto; the protobuf
        # serializer strips default-valued optional fields, so we can't
        # rely on lat/lon being present (fixed-position without stored
        # coords is a valid zero-state). What we CAN rely on is the
        # outer `position` sub-dict existing and the `raw` protobuf
        # parsing cleanly — those prove the POSITION_APP portnum
        # decoded without errors.
        decoded = got.get("decoded", {})
        assert "position" in decoded, (
            f"packet from {tx_role} had no `decoded.position` — "
            f"POSITION_APP decode failed. decoded={decoded!r}"
        )
    finally:
        _restore_position_config(tx_port, orig_position)
