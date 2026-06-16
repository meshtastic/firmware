# FleetSuite — web UI

Vue 3 + Vite + Tailwind v4 + Pinia single-page app for the Meshtastic test
harness. Replaces the old Textual TUI. It talks to the FastAPI backend in
`../src/meshtastic_mcp/web` over REST + a single `/ws` WebSocket.

## Develop

```bash
# from mcp-server/: runs the backend + this dev server together (HMR)
./scripts/web-dev.sh
```

or manually:

```bash
# terminal 1 — backend
cd mcp-server && .venv/bin/python -m uvicorn meshtastic_mcp.web.app:create_app \
  --factory --port 8765 --reload
# terminal 2 — frontend (proxies /api + /ws → :8765)
cd mcp-server/web-ui && npm install && npm run dev
```

## Build (production)

```bash
npm run build      # emits into ../src/meshtastic_mcp/web/static (gitignored)
```

Then `meshtastic-mcp-web` serves that build and the API on one port and opens a
pywebview window (`--browser` to serve only).

## Layout

- `stores/` — Pinia stores. `ws.ts` owns the single WebSocket and dispatches
  topic-tagged frames; `devices` / `cameras` / `firmware` / `tests` hydrate via
  REST and apply live deltas.
- `components/` — `DeviceCard` (keyed by serial → follows the device across
  ports), `CameraFeed` (MJPEG `<img>`), `TestDashboard` (counters/tree/logs),
  etc.
- `api/client.ts` — thin REST wrappers (relative URLs; same build works behind
  pywebview and the dev proxy).

## macOS camera permission

`cv2.VideoCapture` inherits the launching process's TCC Camera grant. Launched
from a terminal, the first stream prompts for permission. A packaged `.app`
would need `NSCameraUsageDescription`.
