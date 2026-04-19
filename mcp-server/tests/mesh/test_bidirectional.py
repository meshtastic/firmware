"""Mesh: explicit two-way communication, single pass/fail.

Opens a ReceiveCollector on EVERY role, sends a uniquely-tagged broadcast
from each role in turn, and asserts every OTHER role saw it. One atomic
test that answers "is the mesh actually working both directions?".

Not parametrized — it inherently involves the full hub.
"""

from __future__ import annotations

import time
from typing import Any

import pytest

from ._receive import ReceiveCollector


@pytest.mark.timeout(300)
def test_bidirectional_mesh_communication(
    baked_mesh: dict[str, Any],
) -> None:
    """Requires ≥2 baked roles.

    For each role, broadcast a unique tag. Assert every other role's
    ReceiveCollector saw that tag within a 120s window per direction.
    """
    roles = sorted(baked_mesh.keys())
    if len(roles) < 2:
        pytest.skip(f"need ≥2 roles; have {roles!r}")

    # Open receive collectors on every role BEFORE sending anything.
    collectors: dict[str, ReceiveCollector] = {}
    try:
        for role in roles:
            rx = ReceiveCollector(
                baked_mesh[role]["port"], topic="meshtastic.receive.text"
            )
            rx.__enter__()
            collectors[role] = rx

        # Let the meshtastic interfaces stabilize before the first send
        time.sleep(2.0)

        # From each role, send a uniquely-tagged broadcast. We MUST send through
        # the already-open collector — opening a new SerialInterface here would
        # race the collector's exclusive lock on the port.
        tags: dict[str, str] = {}
        for sender in roles:
            tag = f"bidi-{sender}-{int(time.time() * 1000) % 100_000}"
            tags[sender] = tag
            collectors[sender].send_text(tag)
            # Small gap so airtime doesn't overlap
            time.sleep(4.0)

        # Every OTHER role must see every sender's tag within 120s each
        missing: list[str] = []
        for sender, tag in tags.items():
            for receiver in roles:
                if receiver == sender:
                    continue
                got = collectors[receiver].wait_for(
                    lambda pkt, t=tag: pkt.get("decoded", {}).get("text") == t,
                    timeout=120,
                )
                if got is None:
                    observed = [
                        p.get("decoded", {}).get("text")
                        for p in collectors[receiver].snapshot()
                    ]
                    missing.append(
                        f"{sender}->{receiver}: tag {tag!r} not seen; "
                        f"receiver got {observed!r}"
                    )

        assert not missing, "bidirectional comms incomplete:\n  " + "\n  ".join(missing)
    finally:
        for rx in collectors.values():
            try:
                rx.__exit__(None, None, None)
            except Exception:
                pass
