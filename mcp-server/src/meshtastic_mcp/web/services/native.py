"""Native (Docker ``meshtasticd``) node lifecycle.

Each native node is a container publishing the simulator's TCP API (4403) on a
host port, mirrored into the device registry as a ``native:<name>`` device so
the same card/UI applies. Best-effort: every Docker call is guarded and the
service reports ``docker: False`` rather than raising when Docker is absent.
"""

from __future__ import annotations

import asyncio
import logging
import shutil

from ..db import repo_devices as rd
from . import builder

log = logging.getLogger("meshtastic_mcp.web.native")

IMAGE = "meshtastic/meshtasticd:latest"
_PREFIX = "fleetsuite-native-"


def docker_available() -> bool:
    return builder.docker_available()


async def _docker(*args: str, timeout: int = 30) -> tuple[int, str]:
    if not shutil.which("docker"):
        return 127, "docker not found"
    proc = await asyncio.create_subprocess_exec(
        "docker",
        *args,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT,
    )
    try:
        out, _ = await asyncio.wait_for(proc.communicate(), timeout=timeout)
    except asyncio.TimeoutError:
        proc.kill()
        return 124, "docker timed out"
    return proc.returncode, out.decode(errors="replace")


def _container(name: str) -> str:
    return f"{_PREFIX}{name}"


async def info(db) -> dict:
    nodes = [
        d for d in await rd.list_all(db) if d.get("kind") == "native"
    ]
    return {"docker": docker_available(), "image": IMAGE, "nodes": nodes}


async def create(db, *, name: str, tcp_port: int) -> dict:
    code, out = await _docker(
        "run",
        "-d",
        "--name",
        _container(name),
        "-p",
        f"{tcp_port}:4403",
        IMAGE,
    )
    if code != 0:
        raise RuntimeError(f"docker run failed: {out.strip()[:200]}")
    return await rd.upsert_native(db, name=name, tcp_port=tcp_port, online=True)


async def lifecycle(db, name: str, action: str) -> dict:
    verb = {"start": "start", "stop": "stop", "restart": "restart"}.get(action)
    if verb is None:
        raise ValueError(f"unknown action: {action}")
    code, out = await _docker(verb, _container(name))
    if code != 0:
        raise RuntimeError(f"docker {verb} failed: {out.strip()[:200]}")
    online = action != "stop"
    row = await rd.get(db, f"native:{name}")
    tcp_port = row.get("tcp_port") if row else 4403
    return await rd.upsert_native(db, name=name, tcp_port=tcp_port, online=online)


async def remove(db, name: str) -> None:
    await _docker("rm", "-f", _container(name))
    await db.execute("DELETE FROM devices WHERE serial_number=?", (f"native:{name}",))
