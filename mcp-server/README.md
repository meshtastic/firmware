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

## Tools (38)

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

## Safety

- **All destructive flash/admin tools require `confirm=True`** as a tool-level gate, on top of any permission prompt from Claude.
- **Serial port is exclusive.** If a `serial_*` session is active on a port, `device_info`/admin tools on the same port will fail fast with a pointer at the active `session_id`. Close the session first.
- **Flash confirmation by architecture**: `erase_and_flash` / `update_flash` error if the env's architecture isn't ESP32 — use `pio_flash` for nRF52/RP2040/STM32.

## Environment variables

| Var                        | Default                                                     | Purpose                 |
| -------------------------- | ----------------------------------------------------------- | ----------------------- |
| `MESHTASTIC_FIRMWARE_ROOT` | walks up from cwd for `platformio.ini`                      | Pin the firmware repo   |
| `MESHTASTIC_PIO_BIN`       | `~/.platformio/penv/bin/pio` → `$PATH` `pio` → `platformio` | Override `pio` location |
| `MESHTASTIC_ESPTOOL_BIN`   | `<firmware>/.venv/bin/esptool` → `$PATH`                    | Override esptool        |
| `MESHTASTIC_NRFUTIL_BIN`   | `$PATH`                                                     | Override nrfutil        |
| `MESHTASTIC_PICOTOOL_BIN`  | `$PATH`                                                     | Override picotool       |

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
