#!/usr/bin/env bash
# Run the FleetSuite backend (uvicorn, :8765) and the Vite dev server together,
# with HMR. The Vite server proxies /api and /ws to the backend. Ctrl-C stops
# both. For a single-process production run instead, build the SPA once
# (`cd web-ui && npm run build`) and launch `meshtastic-mcp-web`.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PY="${ROOT}/.venv/bin/python"
if [[ ! -x $PY ]]; then
	echo "No venv at .venv — run: python3 -m venv .venv && .venv/bin/pip install -e '.[web,test]'" >&2
	exit 1
fi

cleanup() {
	trap - INT TERM
	[[ -n ${BACK_PID-} ]] && kill "$BACK_PID" 2>/dev/null || true
	[[ -n ${UI_PID-} ]] && kill "$UI_PID" 2>/dev/null || true
}
trap cleanup INT TERM EXIT

echo "[web-dev] starting backend on http://127.0.0.1:8765 …"
"$PY" -m uvicorn meshtastic_mcp.web.app:create_app --factory --port 8765 --reload &
BACK_PID=$!

echo "[web-dev] starting Vite dev server …"
(cd web-ui && npm run dev) &
UI_PID=$!

wait
