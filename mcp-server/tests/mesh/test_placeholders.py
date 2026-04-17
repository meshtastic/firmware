"""Mesh placeholders: v2 roadmap entries, skipped in v2.0."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.skip(reason="placeholder — implement in v2.1")


def test_direct_to_ghost_times_out_or_naks() -> None:
    """Send a direct message to a fabricated node_num (e.g. 0xDEADBEEF) with
    want_ack=True; assert caller observes NAK or timeout within 60s. Operator
    fleet-health check — 'is my offline-peer detection working'."""
    pass


def test_hop_limit_decrements() -> None:
    """Send a message with hop_limit=3; inspect received packet on B via
    serial decoder; assert hop_limit=2 (1-hop decrement across A→B). Prevents
    runaway rebroadcasting that destroys mesh throughput."""
    pass


def test_snr_rssi_populated() -> None:
    """After 2 min of idle mesh traffic, B's entry in A's node DB must have
    `snr ∈ [-25, 15]` and `rssi ∈ [-140, 0]`. The most common 'my mesh is
    broken' misdiagnosis is actually just an empty node DB."""
    pass


def test_secondary_channel_stays_private() -> None:
    """Bake A with channels [primary + secondary 'Secret' PSK K1]; bake B
    with primary only. A sends on ch_index=1; B's decoded log must NOT contain
    the secret payload. The privacy-by-channel promise."""
    pass


def test_psk_mismatch_no_decode() -> None:
    """Bake A with psk_seed='run-A', B with psk_seed='run-B' (same
    channel_name). A sends broadcast; B's log shows packet-received-but-
    rejected at decode (PSK mismatch). Proves session isolation between
    concurrent test labs."""
    pass


def test_traceroute_one_hop() -> None:
    """A → traceroute(B); response path contains exactly [A, B] with no
    intermediate routers (since there aren't any in a 2-device mesh).
    Validates `traceroute` packet round-trip."""
    pass
