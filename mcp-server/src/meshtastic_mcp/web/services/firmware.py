"""Current firmware git ref — branch, sha, dirty flag, and the tip commit's
subject/date. Read straight from git in the firmware root; degrades to
``{"available": False}`` outside a checkout."""

from __future__ import annotations

import subprocess
from functools import lru_cache
from pathlib import Path


@lru_cache(maxsize=1)
def _root() -> Path:
    try:
        from meshtastic_mcp import config as mcfg

        return mcfg.firmware_root()
    except Exception:  # noqa: BLE001
        return Path.cwd()


def _git(*args: str) -> str | None:
    try:
        out = subprocess.run(
            ["git", *args],
            cwd=str(_root()),
            capture_output=True,
            text=True,
            timeout=10,
        )
        if out.returncode != 0:
            return None
        return out.stdout.strip()
    except Exception:  # noqa: BLE001
        return None


def firmware_ref() -> dict:
    sha = _git("rev-parse", "HEAD")
    if not sha:
        return {"available": False}
    branch = _git("rev-parse", "--abbrev-ref", "HEAD")
    status = _git("status", "--porcelain")
    subject = _git("log", "-1", "--pretty=%s")
    committed_at = _git("log", "-1", "--pretty=%cI")
    return {
        "available": True,
        "branch": branch,
        "sha": sha,
        "short_sha": sha[:7],
        "dirty": bool(status),
        "subject": subject,
        "committed_at": committed_at,
    }
