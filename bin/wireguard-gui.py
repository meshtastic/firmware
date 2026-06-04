#!/usr/bin/env python3
"""Simple desktop client for Meshtastic WireGuard configuration."""

from __future__ import annotations

import importlib.util
import json
import queue
import sys
import threading
import time
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from typing import Any, Callable


SCRIPT_DIR = Path(__file__).resolve().parent
BUNDLE_DIR = Path(getattr(sys, "_MEIPASS", SCRIPT_DIR))
CONFIG_SCRIPT = BUNDLE_DIR / "wireguard-config.py"


def _load_config_api():
    spec = importlib.util.spec_from_file_location("wireguard_config", CONFIG_SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {CONFIG_SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


wg_api = _load_config_api()


class WireGuardClient(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("Meshtastic WireGuard")
        self.minsize(760, 560)

        self._events: queue.Queue[tuple[str, Any]] = queue.Queue()
        self._monitoring = False
        self._monitor_interval = 10.0
        self._monitor_job: str | None = None
        self._busy = False
        self._health = {
            "connected": False,
            "heartbeat": "",
            "polls": 0,
            "failures": 0,
            "rx_bytes": 0,
            "tx_bytes": 0,
        }

        self._build_vars()
        self._build_ui()
        self._refresh_ports()
        self.after(100, self._drain_events)

    def _build_vars(self) -> None:
        self.port_var = tk.StringVar()
        self.config_path_var = tk.StringVar()
        self.status_var = tk.StringVar(value="Idle")
        self.connection_var = tk.StringVar(value="Disconnected")
        self.wg_status_var = tk.StringVar(value="-")
        self.endpoint_var = tk.StringVar(value="-")
        self.address_var = tk.StringVar(value="-")
        self.last_error_var = tk.StringVar(value="-")
        self.heartbeat_var = tk.StringVar(value="-")
        self.rx_var = tk.StringVar(value="0")
        self.tx_var = tk.StringVar(value="0")
        self.polls_var = tk.StringVar(value="0")
        self.failures_var = tk.StringVar(value="0")

    def _build_ui(self) -> None:
        root = ttk.Frame(self, padding=16)
        root.grid(row=0, column=0, sticky="nsew")
        self.columnconfigure(0, weight=1)
        self.rowconfigure(0, weight=1)
        root.columnconfigure(1, weight=1)
        root.rowconfigure(4, weight=1)

        ttk.Label(root, text="Port").grid(row=0, column=0, sticky="w")
        port_row = ttk.Frame(root)
        port_row.grid(row=0, column=1, sticky="ew", padx=(8, 0))
        port_row.columnconfigure(0, weight=1)
        self.port_combo = ttk.Combobox(port_row, textvariable=self.port_var, state="readonly")
        self.port_combo.grid(row=0, column=0, sticky="ew")
        ttk.Button(port_row, text="Refresh", command=self._refresh_ports).grid(row=0, column=1, padx=(8, 0))

        ttk.Label(root, text="Config").grid(row=1, column=0, sticky="w", pady=(10, 0))
        config_row = ttk.Frame(root)
        config_row.grid(row=1, column=1, sticky="ew", padx=(8, 0), pady=(10, 0))
        config_row.columnconfigure(0, weight=1)
        ttk.Entry(config_row, textvariable=self.config_path_var).grid(row=0, column=0, sticky="ew")
        ttk.Button(config_row, text="Browse", command=self._browse_config).grid(row=0, column=1, padx=(8, 0))

        actions = ttk.Frame(root)
        actions.grid(row=2, column=1, sticky="ew", padx=(8, 0), pady=(12, 0))
        ttk.Button(actions, text="Push Config", command=self._push_config).grid(row=0, column=0)
        ttk.Button(actions, text="Read Device", command=self._read_device).grid(row=0, column=1, padx=(8, 0))
        self.monitor_button = ttk.Button(actions, text="Start Monitor", command=self._toggle_monitor)
        self.monitor_button.grid(row=0, column=2, padx=(8, 0))
        ttk.Label(actions, textvariable=self.status_var).grid(row=0, column=3, padx=(14, 0), sticky="w")

        health = ttk.LabelFrame(root, text="Health", padding=12)
        health.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(16, 0))
        for col in range(4):
            health.columnconfigure(col, weight=1)

        self._metric(health, 0, 0, "Connection", self.connection_var)
        self._metric(health, 0, 1, "WireGuard", self.wg_status_var)
        self._metric(health, 0, 2, "Heartbeat", self.heartbeat_var)
        self._metric(health, 0, 3, "Failures", self.failures_var)
        self._metric(health, 1, 0, "Address", self.address_var)
        self._metric(health, 1, 1, "Endpoint", self.endpoint_var)
        self._metric(health, 1, 2, "RX bytes est.", self.rx_var)
        self._metric(health, 1, 3, "TX bytes est.", self.tx_var)
        self._metric(health, 2, 0, "Polls", self.polls_var)
        self._metric(health, 2, 1, "Last Error", self.last_error_var)

        log_frame = ttk.LabelFrame(root, text="Log", padding=8)
        log_frame.grid(row=4, column=0, columnspan=2, sticky="nsew", pady=(16, 0))
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)
        self.log = tk.Text(log_frame, height=12, wrap="word", state="disabled")
        self.log.grid(row=0, column=0, sticky="nsew")
        scrollbar = ttk.Scrollbar(log_frame, orient="vertical", command=self.log.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.log.configure(yscrollcommand=scrollbar.set)

    def _metric(self, parent: ttk.Frame, row: int, col: int, label: str, var: tk.StringVar) -> None:
        frame = ttk.Frame(parent)
        frame.grid(row=row, column=col, sticky="ew", padx=6, pady=5)
        ttk.Label(frame, text=label).grid(row=0, column=0, sticky="w")
        ttk.Label(frame, textvariable=var, font=("", 10, "bold")).grid(row=1, column=0, sticky="w")

    def _refresh_ports(self) -> None:
        try:
            ports = wg_api.list_serial_ports()
        except Exception as exc:
            self._log(f"Port refresh failed: {exc}")
            return

        values = [f"{port['device']} - {port['description']}" for port in ports]
        self.port_combo.configure(values=values)
        if values and not self.port_var.get():
            self.port_var.set(values[0])
        self._log(f"Found {len(values)} serial port(s).")

    def _browse_config(self) -> None:
        filename = filedialog.askopenfilename(
            title="Select WireGuard config",
            filetypes=[("WireGuard config", "*.conf"), ("All files", "*.*")],
        )
        if filename:
            self.config_path_var.set(filename)

    def _selected_port(self) -> str:
        value = self.port_var.get().strip()
        return value.split(" - ", 1)[0] if value else ""

    def _run_worker(self, name: str, target: Callable[[], Any]) -> None:
        if self._busy:
            self._log("Another device operation is already running.")
            return
        self._busy = True
        self.status_var.set(name)

        def runner() -> None:
            try:
                self._events.put(("result", (name, target())))
            except Exception as exc:
                self._events.put(("error", (name, exc)))

        threading.Thread(target=runner, daemon=True).start()

    def _push_config(self) -> None:
        port = self._selected_port()
        config_path = self.config_path_var.get().strip()
        if not port:
            messagebox.showerror("Missing Port", "Select a serial port.")
            return
        if not config_path:
            messagebox.showerror("Missing Config", "Select a WireGuard config file.")
            return

        self._run_worker(
            "Pushing config...",
            lambda: wg_api.set_wireguard_config(port, config_path, enable=True),
        )
        self._health["tx_bytes"] += Path(config_path).stat().st_size
        self._sync_health()

    def _read_device(self) -> None:
        port = self._selected_port()
        if not port:
            messagebox.showerror("Missing Port", "Select a serial port.")
            return

        self._health["tx_bytes"] += 64
        self._sync_health()
        self._run_worker("Reading device...", lambda: wg_api.read_wireguard_config(port))

    def _toggle_monitor(self) -> None:
        self._monitoring = not self._monitoring
        self.monitor_button.configure(text="Stop Monitor" if self._monitoring else "Start Monitor")
        if self._monitoring:
            self._schedule_monitor(0)
        elif self._monitor_job:
            self.after_cancel(self._monitor_job)
            self._monitor_job = None

    def _schedule_monitor(self, delay_ms: int | None = None) -> None:
        if not self._monitoring:
            return
        delay = int(self._monitor_interval * 1000) if delay_ms is None else delay_ms
        self._monitor_job = self.after(delay, self._monitor_tick)

    def _monitor_tick(self) -> None:
        self._monitor_job = None
        if self._busy:
            self._schedule_monitor()
            return
        port = self._selected_port()
        if not port:
            self._schedule_monitor()
            return
        self._health["tx_bytes"] += 64
        self._sync_health()
        self._run_worker("Polling health...", lambda: wg_api.read_wireguard_config(port))
        self._schedule_monitor()

    def _drain_events(self) -> None:
        while True:
            try:
                event, payload = self._events.get_nowait()
            except queue.Empty:
                break
            if event == "result":
                self._handle_result(*payload)
            elif event == "error":
                self._handle_error(*payload)
        self.after(100, self._drain_events)

    def _handle_result(self, name: str, payload: Any) -> None:
        self._busy = False
        self.status_var.set("Idle")
        if isinstance(payload, dict) and "confirmed" in payload:
            self._log_json("Written", payload["written"])
            self._log_json("Confirmed", payload["confirmed"])
            self._update_config_status(payload["confirmed"])
        else:
            self._log_json(name, payload)
            self._update_config_status(payload)

    def _handle_error(self, name: str, exc: Exception) -> None:
        self._busy = False
        self.status_var.set("Idle")
        self._health["connected"] = False
        self._health["failures"] += 1
        self.connection_var.set("Disconnected")
        self.failures_var.set(str(self._health["failures"]))
        self._log(f"{name} failed: {exc}")

    def _update_config_status(self, config: dict[str, Any]) -> None:
        self._health["connected"] = True
        self._health["polls"] += 1
        self._health["heartbeat"] = time.strftime("%H:%M:%S")
        self._health["rx_bytes"] += len(json.dumps(config))

        self.connection_var.set("Connected")
        self.wg_status_var.set(str(config.get("status", "-")))
        self.address_var.set(str(config.get("address", "-")) or "-")
        server = str(config.get("server_addr", "") or "")
        port = str(config.get("server_port", "") or "")
        self.endpoint_var.set(f"{server}:{port}" if server and port else "-")
        self.last_error_var.set(str(config.get("last_error", "") or "-"))
        self._sync_health()

    def _sync_health(self) -> None:
        self.heartbeat_var.set(self._health["heartbeat"] or "-")
        self.rx_var.set(str(self._health["rx_bytes"]))
        self.tx_var.set(str(self._health["tx_bytes"]))
        self.polls_var.set(str(self._health["polls"]))
        self.failures_var.set(str(self._health["failures"]))

    def _log_json(self, title: str, payload: Any) -> None:
        self._log(f"{title}:\n{json.dumps(payload, indent=2)}")

    def _log(self, message: str) -> None:
        timestamp = time.strftime("%H:%M:%S")
        self.log.configure(state="normal")
        self.log.insert("end", f"[{timestamp}] {message}\n")
        self.log.see("end")
        self.log.configure(state="disabled")


def main() -> None:
    app = WireGuardClient()
    app.mainloop()


if __name__ == "__main__":
    main()
