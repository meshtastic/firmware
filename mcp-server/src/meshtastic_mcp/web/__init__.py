"""FleetSuite — the FastAPI + WebSocket backend that serves the Vue SPA and
drives the Meshtastic test harness. Replaces the old Textual TUI.

Layout:
  db/        — aiosqlite registry (devices, cameras, flash, runs, builds, settings)
  services/  — identity reconciliation, control gating, firmware builds, the
               pytest runner, the Datadog forwarder
  ws/        — the single broadcast hub backing ``/ws``
  app.py     — ``create_app()`` factory (REST + ``/ws``, serves ``web/static``)
  __main__.py — ``main()``: serve + open a pywebview window (``--browser`` to skip it)
"""
