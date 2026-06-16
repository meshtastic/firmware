"""Phase 2 build-orchestrator logic: queue → build → SHA-keyed cache hit.

Uses an injected fake build function that writes a dummy artifact, so the
queue/cache/dedup logic is exercised without Docker or a real compile.
"""

from __future__ import annotations

import asyncio
from pathlib import Path

from meshtastic_mcp.web.db import repo_builds as rb
from meshtastic_mcp.web.db.database import Database
from meshtastic_mcp.web.services import builder
from meshtastic_mcp.web.ws.hub import Hub


def test_build_then_cache_hit(tmp_path, monkeypatch):
    monkeypatch.setenv("MESHTASTIC_MCP_ARTIFACT_DIR", str(tmp_path / "artifacts"))

    calls = {"n": 0}

    def fake_build(env: str, out: Path, log_cb) -> bool:
        calls["n"] += 1
        out.mkdir(parents=True, exist_ok=True)
        (out / f"firmware-{env}-1.0.factory.bin").write_bytes(b"\x00\x01")
        (out / f"firmware-{env}-1.0.mt.json").write_text("{}")
        log_cb("built")
        return True

    async def go():
        db = Database(path=tmp_path / "registry.db")
        await db.connect()
        hub = Hub()
        hub.bind_loop(asyncio.get_running_loop())
        orch = builder.BuildOrchestrator(db, hub, build_fn=fake_build)

        # First enqueue → builds.
        res = await orch.enqueue(["heltec-v3"], sha="abc123", branch="develop")
        assert res and res[0]["status"] == "queued"
        await asyncio.gather(*orch._inflight.values())

        row = await rb.get(db, "heltec-v3", "abc123")
        assert row["status"] == "success", row
        assert builder.cached_artifact_dir("abc123", "heltec-v3") is not None
        assert calls["n"] == 1

        # Second enqueue at the same SHA → cache hit, no rebuild.
        res2 = await orch.enqueue(["heltec-v3"], sha="abc123", branch="develop")
        assert res2[0].get("cached") is True
        assert calls["n"] == 1  # fake_build NOT called again

        # A different SHA → builds again.
        res3 = await orch.enqueue(["heltec-v3"], sha="def456", branch="develop")
        await asyncio.gather(*orch._inflight.values())
        assert calls["n"] == 2
        assert (await rb.get(db, "heltec-v3", "def456"))["status"] == "success"

        await db.close()

    asyncio.run(go())


def test_build_failure_recorded(tmp_path, monkeypatch):
    monkeypatch.setenv("MESHTASTIC_MCP_ARTIFACT_DIR", str(tmp_path / "artifacts"))

    def failing_build(env: str, out: Path, log_cb) -> bool:
        log_cb("boom")
        return False

    async def go():
        db = Database(path=tmp_path / "registry.db")
        await db.connect()
        hub = Hub()
        hub.bind_loop(asyncio.get_running_loop())
        orch = builder.BuildOrchestrator(db, hub, build_fn=failing_build)
        await orch.enqueue(["rak4631"], sha="aaa", branch="develop")
        await asyncio.gather(*orch._inflight.values())
        row = await rb.get(db, "rak4631", "aaa")
        assert row["status"] == "failed"
        assert builder.cached_artifact_dir("aaa", "rak4631") is None
        await db.close()

    asyncio.run(go())
