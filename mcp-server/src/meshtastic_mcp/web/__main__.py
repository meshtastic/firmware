"""FleetSuite entrypoint (the ``meshtastic-mcp-web`` console script).

Default: serve the API + built SPA on 127.0.0.1:8765 and open a pywebview
desktop window pointed at it. ``--browser`` serves only (no window) so you can
open it in any browser — also the mode used in headless/CI and by agents.
"""

from __future__ import annotations

import argparse
import logging
import threading
import time

import uvicorn

HOST = "127.0.0.1"
PORT = 8765


def _serve(server: uvicorn.Server) -> None:
    server.run()


def main() -> None:
    parser = argparse.ArgumentParser(prog="meshtastic-mcp-web", description=__doc__)
    parser.add_argument(
        "--browser",
        action="store_true",
        help="serve only (no desktop window) — open http://127.0.0.1:8765 yourself",
    )
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s"
    )

    config = uvicorn.Config(
        "meshtastic_mcp.web.app:create_app",
        factory=True,
        host=args.host,
        port=args.port,
        log_level="info",
    )
    server = uvicorn.Server(config)

    if args.browser:
        server.run()
        return

    # Desktop window: run uvicorn on a background thread, open pywebview on the
    # main thread (some platforms require the GUI loop to own the main thread).
    try:
        import webview  # type: ignore
    except Exception:  # noqa: BLE001
        logging.warning("pywebview unavailable — falling back to --browser mode")
        server.run()
        return

    thread = threading.Thread(target=_serve, args=(server,), daemon=True)
    thread.start()

    # Wait for the server to bind before pointing the window at it.
    deadline = time.monotonic() + 15
    while not server.started and time.monotonic() < deadline:
        time.sleep(0.1)

    webview.create_window(
        "FleetSuite", f"http://{args.host}:{args.port}", width=1400, height=900
    )
    webview.start()
    server.should_exit = True
    thread.join(timeout=5)


if __name__ == "__main__":
    main()
