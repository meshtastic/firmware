# Meshtastic MCP Server

An [MCP](https://modelcontextprotocol.io) server for working with the Meshtastic firmware repo and connected devices. Lets Claude Code / Claude Desktop:

- Discover USB-connected Meshtastic devices
- Enumerate PlatformIO board variants (166+) with Meshtastic metadata
- Build, clean, flash, erase-and-flash (factory), and OTA-update firmware
- Read serial logs via `pio device monitor` (with board-specific exception decoders)
- Trigger 1200bps touch-reset for bootloader entry (nRF52, ESP32-S3, RP2040)
- Query and administer a running node via the [`meshtastic` Python API](https://github.com/meshtastic/python): owner name, config (LocalConfig + ModuleConfig), channels, messaging, reboot/shutdown/factory-reset
- Call `esptool`, `nrfutil`, `picotool` directly when PlatformIO doesn't cover the operation

## Design principle

**PlatformIO first.** Its `pio run -t upload` knows the correct protocol, offsets, and post-build chain for every variant in `variants/`. Direct vendor-tool wrappers (`esptool_*`, `nrfutil_*`, `picotool_*`) exist as escape hatches for operations pio doesn't cover (blank-chip erase, DFU `.zip` packages, BOOTSEL-mode inspection).

## Prerequisites

- Python ≥ 3.11
- [PlatformIO Core](https://platformio.org/install/cli) — `pio` on `$PATH` or at `~/.platformio/penv/bin/pio`
- The Meshtastic firmware repo checked out somewhere (set via `MESHTASTIC_FIRMWARE_ROOT`)
- Optional: `esptool`, `nrfutil`, `picotool` on `$PATH` (or under the firmware venv at `.venv/bin/`) if you want to use the direct-tool wrappers

## Install

```bash
cd <firmware-repo>/mcp-server
python3 -m venv .venv
.venv/bin/pip install -e .
```

Verify:

```bash
MESHTASTIC_FIRMWARE_ROOT=<firmware-repo> .venv/bin/python -m meshtastic_mcp
```

The server blocks on stdin (that's correct — it speaks MCP over stdio). Ctrl-C to exit.

## Register with Claude Code

Edit `~/.claude/settings.json` (global) or `<firmware-repo>/.claude/settings.local.json` (project-only):

```json
{
  "mcpServers": {
    "meshtastic": {
      "command": "<firmware-repo>/mcp-server/.venv/bin/python",
      "args": ["-m", "meshtastic_mcp"],
      "env": {
        "MESHTASTIC_FIRMWARE_ROOT": "<firmware-repo>"
      }
    }
  }
}
```

Replace `<firmware-repo>` with the absolute path, e.g. `/Users/you/GitHub/firmware`. Restart Claude Code after editing.

## Register with Claude Desktop

Same `mcpServers` block, but in `~/Library/Application Support/Claude/claude_desktop_config.json` (macOS) or `%APPDATA%\Claude\claude_desktop_config.json` (Windows).

## Tools (43)

### Discovery & metadata

| Tool           | What it does                                                                               |
| -------------- | ------------------------------------------------------------------------------------------ |
| `list_devices` | USB/serial port listing, flags likely-Meshtastic candidates                                |
| `list_boards`  | PlatformIO envs with `custom_meshtastic_*` metadata; filters by arch/supported/query/level |
| `get_board`    | Full env dict incl. raw pio config                                                         |

### Build & flash

| Tool              | What it does                                                         |
| ----------------- | -------------------------------------------------------------------- |
| `build`           | `pio run -e <env>` (+ mtjson target)                                 |
| `clean`           | `pio run -e <env> -t clean`                                          |
| `pio_flash`       | `pio run -e <env> -t upload --upload-port <port>` — any architecture |
| `erase_and_flash` | ESP32 full factory flash via `bin/device-install.sh`                 |
| `update_flash`    | ESP32 OTA app-partition update via `bin/device-update.sh`            |
| `touch_1200bps`   | 1200-baud open/close to trigger USB CDC bootloader entry             |

### Serial log sessions

Backed by long-running `pio device monitor` subprocesses with a 10k-line ring buffer per session and board-specific filters (`esp32_exception_decoder` auto-selected when you pass `env=`).

| Tool           | What it does                                                       |
| -------------- | ------------------------------------------------------------------ |
| `serial_open`  | Start a monitor session; returns `session_id`                      |
| `serial_read`  | Cursor-based pull; reports `dropped` if lines aged out of the ring |
| `serial_list`  | All active sessions                                                |
| `serial_close` | Terminate a session                                                |

### Device reads

| Tool          | What it does                                                                |
| ------------- | --------------------------------------------------------------------------- |
| `device_info` | my_node_num, long/short name, firmware version, region, channel, node count |
| `list_nodes`  | Full node database with position, SNR, RSSI, last_heard, battery            |

_The tool tables below document 38 currently registered MCP server tools._

### Device writes

| Tool                | What it does                                                               |
| ------------------- | -------------------------------------------------------------------------- |
| `set_owner`         | Long name + optional short name (≤4 chars)                                 |
| `get_config`        | One section or all (LocalConfig + ModuleConfig)                            |
| `set_config`        | Dot-path field write: `lora.region`=`"US"`, `device.role`=`"ROUTER"`, etc. |
| `get_channel_url`   | Primary-only or include_all=admin URL                                      |
| `set_channel_url`   | Import channels from a Meshtastic URL                                      |
| `set_debug_log_api` | Enable or disable debug logging for the Meshtastic Python API client       |
| `send_text`         | Broadcast or direct text message                                           |
| `reboot`            | `localNode.reboot(secs)` — requires `confirm=True`                         |
| `shutdown`          | `localNode.shutdown(secs)` — requires `confirm=True`                       |
| `factory_reset`     | `localNode.factoryReset(full?)` — requires `confirm=True`                  |

### Direct hardware tools (escape hatches)

| Tool                  | What it does                                              |
| --------------------- | --------------------------------------------------------- |
| `esptool_chip_info`   | Read chip, MAC, crystal, flash size                       |
| `esptool_erase_flash` | Full-chip erase (destructive)                             |
| `esptool_raw`         | Pass-through; confirm=True required for write/erase/merge |
| `nrfutil_dfu`         | DFU-flash a `.zip` package                                |
| `nrfutil_raw`         | Pass-through                                              |
| `picotool_info`       | Read Pico BOOTSEL-mode info                               |
| `picotool_load`       | Load a UF2                                                |
| `picotool_raw`        | Pass-through                                              |

### USB power control (uhubctl)

| Tool            | What it does                                                |
| --------------- | ----------------------------------------------------------- |
| `uhubctl_list`  | Enumerate USB hubs + attached-device VID/PID (read-only)    |
| `uhubctl_power` | Drive a hub port `on` or `off`; `off` requires confirm=True |
| `uhubctl_cycle` | Off → wait `delay_s` → on; confirm=True required            |

Target a port by explicit `(location, port)` (raw uhubctl syntax like
`location="1-1.3", port=2`) or by `role` (`"nrf52"`, `"esp32s3"`). Role
lookup checks `MESHTASTIC_UHUBCTL_LOCATION_<ROLE>` +
`MESHTASTIC_UHUBCTL_PORT_<ROLE>` env vars first, then auto-detects via VID
against `uhubctl`'s output.

Requires [`uhubctl`](https://github.com/mvp/uhubctl) on PATH:

```bash
brew install uhubctl        # macOS
apt install uhubctl         # Debian/Ubuntu
```

Modern macOS + PPPS-capable hubs generally work without root. On Linux
without udev rules, or on old macOS with driver quirks, you may need
`sudo`. If uhubctl returns a permission error the MCP tool raises a
clear `UhubctlError` pointing at the
[udev-rules / sudo fallback](https://github.com/mvp/uhubctl#linux-usb-permissions)
rather than auto-`sudo`'ing mid-run.

## Safety

- **All destructive flash/admin tools require `confirm=True`** as a tool-level gate, on top of any permission prompt from Claude.
- **Serial port is exclusive.** If a `serial_*` session is active on a port, `device_info`/admin tools on the same port will fail fast with a pointer at the active `session_id`. Close the session first.
- **Flash confirmation by architecture**: `erase_and_flash` / `update_flash` error if the env's architecture isn't ESP32 — use `pio_flash` for nRF52/RP2040/STM32.

## Environment variables

| Var                        | Default                                                     | Purpose                                                             |
| -------------------------- | ----------------------------------------------------------- | ------------------------------------------------------------------- |
| `MESHTASTIC_FIRMWARE_ROOT` | walks up from cwd for `platformio.ini`                      | Pin the firmware repo                                               |
| `MESHTASTIC_PIO_BIN`       | `~/.platformio/penv/bin/pio` → `$PATH` `pio` → `platformio` | Override `pio` location                                             |
| `MESHTASTIC_ESPTOOL_BIN`   | `<firmware>/.venv/bin/esptool` → `$PATH`                    | Override esptool                                                    |
| `MESHTASTIC_NRFUTIL_BIN`   | `$PATH`                                                     | Override nrfutil                                                    |
| `MESHTASTIC_PICOTOOL_BIN`  | `$PATH`                                                     | Override picotool                                                   |
| `MESHTASTIC_MCP_SEED`      | `mcp-<user>-<host>`                                         | PSK seed for test-harness session (CI override)                     |
| `MESHTASTIC_MCP_FLASH_LOG` | `<mcp-server>/tests/flash.log`                              | Tee target for pio/esptool/nrfutil subprocess output (TUI tails it) |

## Hardware Test Suite

`mcp-server/tests/` holds a pytest-based integration suite that exercises
real USB-connected Meshtastic devices against the MCP server surface. Separate
from the native C++ unit tests in the firmware repo's top-level `test/`
directory — this one validates the device-facing behavior end-to-end.

### Invocation

```bash
./mcp-server/run-tests.sh                               # full suite (auto-detect + auto-bake-if-needed)
./mcp-server/run-tests.sh --force-bake                  # reflash devices before testing
./mcp-server/run-tests.sh --assume-baked                # skip the bake step (caller vouches for state)
./mcp-server/run-tests.sh tests/mesh                    # one tier
./mcp-server/run-tests.sh tests/mesh/test_traceroute.py # one file
./mcp-server/run-tests.sh -k telemetry                  # pytest name filter
```

The wrapper auto-detects connected devices (VID `0x239A` → `nrf52` → env
`rak4631`; `0x303A` or `0x10C4` → `esp32s3` → env `heltec-v3`), exports
`MESHTASTIC_MCP_ENV_<ROLE>` env vars, and invokes pytest. Overrides via
per-role env vars: `MESHTASTIC_MCP_ENV_NRF52=heltec-mesh-node-t114 ./run-tests.sh`.

No hardware connected? The wrapper narrows to `tests/unit/` only and says so
in the pre-flight header.

### Tiers (run in this order)

- **`bake`** (`tests/test_00_bake.py`) — flashes both hub roles with the
  session's test profile. Has a skip-if-already-baked check (region + channel
  match); `--force-bake` overrides.
- **`unit`** — pure Python, no hardware. boards / PIO wrapper /
  userPrefs-parse / testing-profile fixtures.
- **`mesh`** — 2-device mesh: formation, broadcast delivery, direct+ACK,
  traceroute, bidirectional. Parametrized over both directions. Includes
  `test_peer_offline_recovery` which uses uhubctl to power-cycle one peer
  mid-conversation and verifies the mesh recovers (skips without uhubctl).
- **`telemetry`** — periodic telemetry broadcast + on-demand request/reply
  (`TELEMETRY_APP` with `wantResponse=True`).
- **`monitor`** — boot log has no panic markers within 60 s of reboot.
- **`recovery`** — `uhubctl` power-cycle round-trip: verifies the hub port
  can be toggled off/on, the device re-enumerates with the same
  `my_node_num`, and NVS-resident config (region, channel, modem preset)
  survives a hard reset. Requires `uhubctl` on PATH; skips cleanly otherwise.
- **`ui`** — input-broker-driven screen navigation (`AdminMessage.send_input_event`
  injection → `Screen::handleInputEvent` → frame transition). Parametrized
  on the screen-bearing role (heltec-v3 OLED). Captures images via USB
  webcam + OCRs them for HTML-report evidence. Requires `pip install -e '.[ui]'`
  and `MESHTASTIC_UI_CAMERA_DEVICE_ESP32S3=<index>`; tier is auto-deselected
  if `cv2` isn't importable.
- **`fleet`** — PSK-seed isolation: two labs with different seeds never
  overlap.
- **`admin`** — owner persistence across reboot, channel URL round-trip,
  `lora.hop_limit` persistence.
- **`provisioning`** — region/channel baking, userPrefs survive
  `factory_reset(full=False)`.

#### UI tier setup

The `tests/ui/` tier drives the on-device OLED via the firmware's existing
`AdminMessage.send_input_event` RPC (no firmware changes required) and
verifies transitions via a macro-gated log line + camera + OCR. Summary:

1. Install extras: `pip install -e 'mcp-server/.[ui]'` — pulls in
   `opencv-python-headless`, `numpy`, `easyocr`, `Pillow`. First easyocr
   run downloads ~100 MB of models to `~/.EasyOCR/`; an autouse session
   fixture pre-warms the reader so per-test OCR is <100 ms after that.
2. Point a USB webcam at the heltec-v3 OLED. Discover its index:
   ```bash
   .venv/bin/python -c "import cv2; [print(i, cv2.VideoCapture(i).read()[0]) for i in range(5)]"
   ```
3. Export the per-role device env var:
   ```bash
   export MESHTASTIC_UI_CAMERA_DEVICE_ESP32S3=0
   ```
4. Run:
   ```bash
   ./run-tests.sh tests/ui -v
   ```
   Captures land under `tests/ui_captures/<session_seed>/<test_id>/`, one
   PNG + `.ocr.txt` per `frame_capture()` call, with a per-test
   `transcript.md` stepping through event → frame → OCR. The HTML report
   embeds the full image strip inline (pass or fail).

On macOS, `cv2.VideoCapture(0)` triggers the TCC Camera permission prompt
on first use. Pre-grant Terminal (or your IDE's terminal) before running.
The `OpenCVBackend` fails fast on 10 consecutive black frames so a silent
permission denial surfaces as a clear error, not an empty PNG strip.

No camera? Set `MESHTASTIC_UI_CAMERA_BACKEND=null` (or leave the device var
unset). Tests still exercise the event-injection path and log assertions;
captures just become 1×1 black PNGs.

### Artifacts (regenerated every run, under `tests/`)

- `report.html` — self-contained pytest-html report. Each test gets a
  **Meshtastic debug** section attached on failure with a 200-line firmware
  log tail + device-state dump. Open this first on failures.
- `junit.xml` — CI-parseable.
- `reportlog.jsonl` — `pytest-reportlog` event stream; consumed by the TUI.
- `fwlog.jsonl` — firmware log mirror (`meshtastic.log.line` pubsub → JSONL).
- `flash.log` — tee of all pio / esptool / nrfutil / picotool subprocess
  output during the run (driven by `MESHTASTIC_MCP_FLASH_LOG`).

### Live TUI

```bash
.venv/bin/meshtastic-mcp-test-tui
.venv/bin/meshtastic-mcp-test-tui tests/mesh    # pytest args pass through
```

Textual-based wrapper over `run-tests.sh` with a live test tree, tier
counters, pytest output pane, firmware-log pane, and a device-status strip.
Key bindings: `r` re-run focused, `f` filter, `d` failure detail, `g` open
`report.html`, `x` export reproducer bundle, `l` cycle fw-log filter, `q`
quit (SIGINT → SIGTERM → SIGKILL escalation).

Set `MESHTASTIC_UI_TUI_CAMERA=1` to mount a bottom-of-screen **UI camera**
panel. Left side: the latest capture PNG rendered as Unicode half-blocks
(via `rich-pixels`, works in any terminal — no kitty/sixel required).
Right side: live transcript tail ("step 3 — frame 4/8 name=nodelist_nodes
— OCR: Nodes 2/2") so you can see every event-injection and its result
as each UI test runs. Requires the `[ui]` extras for image rendering; the
transcript alone works without them.

### Slash commands

Three AI-assisted workflows are wired up for Claude Code operators
(`.claude/commands/`) and Copilot operators (`.github/prompts/`):
`/test` (run + interpret), `/diagnose` (read-only health report), `/repro`
(flake triage, N-times re-run with log diff).

### House rules (for human + agent contributors)

- Session-scoped fixtures in `tests/conftest.py` snapshot + restore
  `userPrefs.jsonc`; **never edit `userPrefs.jsonc` from inside a test**.
  Use the `test_profile` / `no_region_profile` fixtures for ephemeral
  overrides.
- `SerialInterface` holds an **exclusive port lock**; sequence calls
  open → mutate → close, then next device. No parallel calls to the
  same port.
- Directed PKI-encrypted sends need **bilateral NodeInfo warmup** —
  both sides must hold the other's current pubkey. See
  `tests/mesh/_receive.py::nudge_nodeinfo_port` and the three directed-
  send tests (`test_direct_with_ack`, `test_traceroute`,
  `test_telemetry_request_reply`) for the canonical pattern.

## Layout

```text
mcp-server/
├── pyproject.toml
├── README.md
└── src/meshtastic_mcp/
    ├── __main__.py         # entry: python -m meshtastic_mcp
    ├── server.py           # FastMCP app + @app.tool() registrations (thin)
    ├── config.py           # firmware_root, pio_bin, esptool_bin, etc.
    ├── pio.py              # subprocess wrapper (timeouts, JSON, tail_lines)
    ├── devices.py          # list_devices (findPorts + comports)
    ├── boards.py           # list_boards / get_board (pio project config parse + cache)
    ├── flash.py            # build, clean, flash, erase_and_flash, update_flash, touch_1200bps
    ├── serial_session.py   # SerialSession + reader thread + ring buffer
    ├── registry.py         # session registry + per-port locks
    ├── connection.py       # connect(port) ctx mgr — SerialInterface + port lock
    ├── info.py             # device_info, list_nodes
    ├── admin.py            # set_owner, get/set_config, channels, send_text, reboot/shutdown/factory_reset
    └── hw_tools.py         # esptool / nrfutil / picotool wrappers
```

## Troubleshooting

- **"Could not locate Meshtastic firmware root"** — set `MESHTASTIC_FIRMWARE_ROOT`.
- **"Could not find `pio`"** — install PlatformIO or set `MESHTASTIC_PIO_BIN`.
- **"Port is held by serial session ..."** — call `serial_close(session_id)` or `serial_list` to find it.
- **`factory.bin` not found after build** — the env may not be ESP32; only ESP32 envs produce a `.factory.bin`.
- **`touch_1200bps` reported `new_port: null`** — the device may not have 1200bps-reset stdio, or the bootloader re-uses the same port name. Check `list_devices` manually.
