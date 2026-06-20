"""Multi-hop NextHop directed-message delivery + relay-recovery (bench test).

This is the hardware/tier-3 validator for the NextHop DM reliability work
(see `docs/nexthop-routing-reliability.md`). The unit suite
`test/test_nexthop_routing` covers the routing *logic* exhaustively; this test
covers the *end-to-end* multi-hop behavior that only a real (or RF-separated)
mesh exercises:

  * a directed DM that must traverse a relay is delivered (next_hop routing +
    the M1/M2 ambiguity gate + M3 route learning all engage), and
  * when the established relay drops and returns, delivery recovers rather than
    black-holing (the M3 stale-route decay / re-learn path).

TOPOLOGY REQUIREMENT — why this usually SKIPS:
  A NextHop relay only happens when the two endpoints are NOT direct neighbors.
  Three co-located radios all hear each other, so A→C is a single direct hop and
  next_hop never engages. To run this test the bench must be a *line* — A — B — C
  — with the endpoints out of each other's direct RF range (physical distance or
  attenuators). The `multihop_topology` fixture detects this automatically: it
  warms the mesh, looks for a pair that is ≥1 hop apart, confirms the relay via
  traceroute, and `pytest.skip`s cleanly when the bench is all-direct. So this
  file is safe to commit and run anywhere — it only *asserts* when the topology
  genuinely requires a relay.

REQUIREMENTS:
  * ≥3 baked devices. The default hub profile is 2 roles (nrf52, esp32s3); add a
    third via `--hub-profile=path/to/hub.yaml` (see conftest `hub_profile`).
  * The relay-recovery test additionally needs uhubctl + a power-controllable
    relay port (same gate the other power tests use).
"""

from __future__ import annotations

import time
from typing import Any

import pytest
from meshtastic_mcp.connection import connect
from tests import _power
from tests._port_discovery import resolve_port_by_role

from ._receive import ReceiveCollector, nudge_nodeinfo, nudge_nodeinfo_port


def _hops_away(rec: dict[str, Any]) -> int | None:
    """Read a node's hop distance from a `nodesByNum` entry, tolerating either
    the camelCase (`hopsAway`) or snake_case (`hops_away`) spelling depending on
    the meshtastic-python version."""
    for key in ("hopsAway", "hops_away"):
        val = rec.get(key)
        if isinstance(val, int):
            return val
    return None


def _warm_mesh(ports: list[str], rounds: int = 2, settle: float = 6.0) -> None:
    """Flood a fresh NodeInfo from every node so the whole mesh (including
    multi-hop pairs, reached via relayed broadcasts) populates pubkeys and hop
    distances. Best-effort — a single node failing to nudge shouldn't abort."""
    for _ in range(rounds):
        for port in ports:
            try:
                nudge_nodeinfo_port(port)
            except Exception:  # noqa: BLE001 — warmup is best-effort
                pass
            time.sleep(0.5)
        time.sleep(settle)


def _wait_for_pubkey(
    tx_iface: Any, rx_num: int, rx_port: str, deadline_s: float = 90.0
) -> bool:
    """Block until `tx_iface` holds `rx_num`'s public key (directed PKI sends
    NAK without it). Re-nudges both sides periodically; multi-hop warmup is
    slower than the 2-device case because NodeInfo must be relayed, hence the
    longer default deadline."""
    deadline = time.monotonic() + deadline_s
    last_nudge = time.monotonic()
    while time.monotonic() < deadline:
        rec = (tx_iface.nodesByNum or {}).get(rx_num, {})
        if rec.get("user", {}).get("publicKey"):
            return True
        if time.monotonic() - last_nudge > 20.0:
            nudge_nodeinfo_port(rx_port)
            nudge_nodeinfo(tx_iface)
            last_nudge = time.monotonic()
        time.sleep(1.0)
    return False


def _traceroute_route(tx_port: str, rx_num: int, rx_port: str) -> list[int] | None:
    """Run a traceroute TX→RX and return the forward `route` (list of relay node
    numbers), or None if it couldn't be obtained. Mirrors test_traceroute's
    request/PKI/retry pattern."""
    from meshtastic.mesh_interface import MeshInterface

    with ReceiveCollector(tx_port, topic="meshtastic.receive.traceroute") as tx:
        nudge_nodeinfo_port(rx_port)
        tx.broadcast_nodeinfo_ping()
        if not _wait_for_pubkey(tx._iface, rx_num, rx_port, 60.0):
            return None
        for _attempt in range(2):
            try:
                tx._iface.sendTraceRoute(dest=rx_num, hopLimit=5)
                break
            except MeshInterface.MeshInterfaceError:
                time.sleep(5.0)
        else:
            return None
        pkt = tx.wait_for(lambda p: p.get("from") == rx_num, timeout=8.0)
        if pkt is None:
            return None
        tr = (pkt.get("decoded", {}) or {}).get("traceroute") or {}
        return [int(n) for n in (tr.get("route") or [])]


@pytest.fixture(scope="session")
def multihop_topology(baked_mesh: dict[str, Any]) -> dict[str, Any]:
    """Discover a real multi-hop pier (tx → relay → rx) on the bench, or skip.

    Returns {tx_role, tx_port, rx_role, rx_port, rx_num, relay_role, relay_num}.
    """
    roles = sorted(baked_mesh)
    if len(roles) < 3:
        pytest.skip(
            "multi-hop NextHop test needs ≥3 baked devices arranged as a line "
            "(endpoints out of direct RF range). Add a third role via "
            f"--hub-profile. Detected roles: {roles}"
        )

    by_role = {r: (baked_mesh[r]["port"], baked_mesh[r]["my_node_num"]) for r in roles}
    if any(num is None for _, num in by_role.values()):
        pytest.skip("a baked device is missing my_node_num; can't map the topology")

    _warm_mesh([port for port, _ in by_role.values()])

    # Find an ordered pair that is ≥1 hop apart, using each node's own nodeDB
    # (cheap — no traceroute yet). On an all-direct bench nothing qualifies.
    multihop_pair: tuple[str, str] | None = None
    for a_role in roles:
        a_port, _ = by_role[a_role]
        try:
            with connect(port=a_port) as a_iface:
                nodes = a_iface.nodesByNum or {}
        except Exception:  # noqa: BLE001
            continue
        for c_role in roles:
            if c_role == a_role:
                continue
            _, c_num = by_role[c_role]
            hops = _hops_away(nodes.get(c_num, {}))
            if hops is not None and hops >= 1:
                multihop_pair = (a_role, c_role)
                break
        if multihop_pair:
            break

    if not multihop_pair:
        pytest.skip(
            "no multi-hop pair found — every device appears to be a direct "
            "neighbor. Arrange the bench as a line (A — B — C) with the "
            "endpoints out of direct RF range (distance or attenuators) so a "
            "relay is actually required, then re-run."
        )

    a_role, c_role = multihop_pair
    a_port, _ = by_role[a_role]
    c_port, c_num = by_role[c_role]

    route = _traceroute_route(a_port, c_num, c_port)
    if not route:
        pytest.skip(
            f"{a_role}→{c_role} looked multi-hop but traceroute returned no "
            "intermediate relay; can't identify the relay node to drive the "
            "recovery test"
        )

    relay_num = route[0]
    relay_role = next((r for r in roles if by_role[r][1] == relay_num), None)
    return {
        "tx_role": a_role,
        "tx_port": a_port,
        "rx_role": c_role,
        "rx_port": c_port,
        "rx_num": c_num,
        "relay_role": relay_role,
        "relay_num": relay_num,
    }


@pytest.mark.timeout(300)
def test_multihop_dm_delivers(multihop_topology: dict[str, Any]) -> None:
    """A directed wantAck DM that must traverse the relay is delivered.

    Exercises the NextHop routing path end-to-end: TX picks a next hop toward
    RX (M2 gate), the relay resolves the next_hop byte and forwards (M1), and
    the route is learned from the returning ACK (M3). Retries absorb transient
    LoRa loss; the assertion is on eventual delivery.
    """
    tx_port = multihop_topology["tx_port"]
    rx_port = multihop_topology["rx_port"]
    rx_num = multihop_topology["rx_num"]
    tx_role = multihop_topology["tx_role"]
    rx_role = multihop_topology["rx_role"]
    relay_role = multihop_topology["relay_role"]

    unique = f"nexthop-mh-{tx_role}-to-{rx_role}-{int(time.time())}"

    with ReceiveCollector(rx_port, topic="meshtastic.receive.text") as rx:
        rx.broadcast_nodeinfo_ping()
        with connect(port=tx_port) as tx_iface:
            nudge_nodeinfo(tx_iface)
            if not _wait_for_pubkey(tx_iface, rx_num, rx_port, 90.0):
                pytest.skip(
                    f"{tx_role} never learned {rx_role}'s pubkey over the relay; "
                    "multi-hop PKI warmup didn't complete"
                )
            got = None
            for _attempt in range(3):
                pkt = tx_iface.sendText(unique, destinationId=rx_num, wantAck=True)
                assert pkt is not None
                got = rx.wait_for(
                    lambda p: p.get("decoded", {}).get("text") == unique,
                    timeout=45,
                )
                if got is not None:
                    break
                rx.broadcast_nodeinfo_ping()
                nudge_nodeinfo(tx_iface)
                time.sleep(5.0)

    assert got is not None, (
        f"multi-hop directed DM {tx_role}→{rx_role} via relay "
        f"{relay_role!r} never landed — NextHop multi-hop delivery is broken"
    )


@pytest.mark.timeout(600)
def test_multihop_relay_recovery(
    multihop_topology: dict[str, Any],
    power_cycle,  # noqa: ARG001 — forces the uhubctl-availability skip
) -> None:
    """Delivery recovers after the established relay drops and returns.

    Establishes a baseline DM (route via relay learned), powers the relay OFF
    (confirming TX survives sending across a downed relay), then powers it back
    ON and asserts directed delivery resumes — the M3 stale-route decay /
    re-learn path. With a strict A — B — C line there is no path while B is down,
    so we only assert TX doesn't crash during the outage; the delivery assertion
    is after B returns.
    """
    relay_role = multihop_topology["relay_role"]
    if not relay_role:
        pytest.skip(
            "relay node isn't one of the baked hub roles, so it can't be "
            "power-cycled; recovery test needs a controllable relay"
        )

    tx_port = multihop_topology["tx_port"]
    rx_port = multihop_topology["rx_port"]
    rx_num = multihop_topology["rx_num"]
    tx_role = multihop_topology["tx_role"]
    rx_role = multihop_topology["rx_role"]

    base = f"mh-recover-base-{int(time.time())}"
    post = f"mh-recover-post-{int(time.time())}"

    # Baseline: confirm delivery works (so the route via the relay is learned)
    # before we perturb anything — otherwise a later failure is ambiguous.
    with ReceiveCollector(rx_port, topic="meshtastic.receive.text") as rx:
        rx.broadcast_nodeinfo_ping()
        with connect(port=tx_port) as tx_iface:
            nudge_nodeinfo(tx_iface)
            if not _wait_for_pubkey(tx_iface, rx_num, rx_port, 90.0):
                pytest.skip("multi-hop PKI warmup failed; can't run recovery test")
            tx_iface.sendText(base, destinationId=rx_num, wantAck=True)
            assert (
                rx.wait_for(
                    lambda p: p.get("decoded", {}).get("text") == base, timeout=45
                )
                is not None
            ), "baseline multi-hop delivery failed — skipping recovery to avoid a false result"

    # Power the relay OFF.
    try:
        _power.power_off(relay_role)
        _power.wait_for_absence(relay_role, timeout_s=15.0)
    except Exception as exc:  # noqa: BLE001
        try:
            _power.power_on(relay_role)
            resolve_port_by_role(relay_role, timeout_s=30.0)
        except Exception:  # noqa: BLE001
            pass
        pytest.skip(f"can't power-control relay {relay_role!r}: {exc}")

    # With the only relay down there's no path; we just confirm TX accepts the
    # send and survives its internal retries (it must not crash / wedge).
    try:
        with connect(port=tx_port) as tx_iface:
            pkt = tx_iface.sendText(
                f"mh-while-down-{int(time.time())}",
                destinationId=rx_num,
                wantAck=True,
            )
            assert pkt is not None
            time.sleep(8.0)  # let retransmissions + route decay run
    except Exception as exc:  # noqa: BLE001 — restore bench state before failing
        _power.power_on(relay_role)
        resolve_port_by_role(relay_role, timeout_s=30.0)
        raise AssertionError(
            f"TX crashed sending across a downed relay: {exc}"
        ) from exc

    # Power the relay back ON and let it re-enumerate + boot.
    _power.power_on(relay_role)
    time.sleep(0.5)
    try:
        resolve_port_by_role(relay_role, timeout_s=30.0)
    except Exception:  # noqa: BLE001 — relay port isn't one we connect to directly
        pass
    time.sleep(8.0)
    _warm_mesh([tx_port, rx_port], rounds=1)  # re-flood so the relay re-learns

    # Delivery should resume once the relay is back (M3 re-learn / decay path).
    got = None
    with ReceiveCollector(rx_port, topic="meshtastic.receive.text") as rx:
        rx.broadcast_nodeinfo_ping()
        with connect(port=tx_port) as tx_iface:
            nudge_nodeinfo(tx_iface)
            _wait_for_pubkey(tx_iface, rx_num, rx_port, 90.0)
            for _attempt in range(4):
                pkt = tx_iface.sendText(post, destinationId=rx_num, wantAck=True)
                assert pkt is not None
                got = rx.wait_for(
                    lambda p: p.get("decoded", {}).get("text") == post,
                    timeout=45,
                )
                if got is not None:
                    break
                rx.broadcast_nodeinfo_ping()
                nudge_nodeinfo(tx_iface)
                time.sleep(6.0)

    assert got is not None, (
        f"after relay {relay_role!r} returned, multi-hop DM {tx_role}→{rx_role} "
        "never resumed — stale-route recovery (M3) may be broken"
    )
