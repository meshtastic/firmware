# Agent instructions

> **TL;DR**
>
> |                |                                                                                                                        |
> | -------------- | ---------------------------------------------------------------------------------------------------------------------- |
> | Local tests    | `./bin/run-tests.sh` (exit 0 GREEN · 1 RED · 2 AMBER · 3 FILTERED)                                                     |
> | Hardware tests | [meshtastic/meshtastic-mcp](https://github.com/meshtastic/meshtastic-mcp) (`MESHTASTIC_FIRMWARE_ROOT` → this checkout) |
> | Format         | `trunk fmt`                                                                                                            |
> | Mirror docs    | `.github/copilot-instructions.md` (canonical) · `CLAUDE.md` (Claude Code)                                              |
>
> **Need this? It's here.**
>
> |                                             |                                                            |
> | ------------------------------------------- | ---------------------------------------------------------- |
> | General helpers (clamp, UTF-8, string fmt…) | `src/meshUtils.h`                                          |
> | Logging macros (LOG_DEBUG / INFO / WARN…)   | `src/DebugConfiguration.h`                                 |
> | New module skeleton                         | inherit `ProtobufModule<T>` in `src/mesh/ProtobufModule.h` |
> | Observer / event wiring                     | `src/Observer.h`                                           |

This repository is the [Meshtastic](https://meshtastic.org) firmware - a C++17 embedded codebase targeting ESP32 / nRF52 / RP2040 / STM32WL / Linux-Portduino LoRa mesh radios. The Python MCP server that AI agents use to flash, configure, and test connected devices now lives in its own repo, [meshtastic/meshtastic-mcp](https://github.com/meshtastic/meshtastic-mcp); this repo registers it via `.mcp.json` (run through `uvx`) so its tools are available automatically.

## Primary instruction file

**Read `.github/copilot-instructions.md` first.** That file is the canonical agent-facing document for this repo. It covers project layout, coding conventions (naming, module framework, Observer pattern, thread safety), the build system, CI/CD, the native C++ test suite, and - most importantly for automation work - the **MCP Server & Hardware Test Harness** section. Read it top-to-bottom before starting any non-trivial change.

This file (`AGENTS.md`) is a short pointer + quick reference for agents that don't read `.github/copilot-instructions.md` by default.

## Quick command reference

| Action                           | Command                                                                                                                                                               |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Build a firmware variant         | `pio run -e <env>` (e.g. `pio run -e rak4631`, `pio run -e heltec-v3`)                                                                                                |
| Build native macOS host binary   | `pio run -e native-macos` (Homebrew prereqs + CH341 LoRa setup in `variants/native/portduino/platformio.ini`)                                                         |
| Clean + rebuild                  | `pio run -e <env> -t clean && pio run -e <env>`                                                                                                                       |
| Flash a device                   | `pio run -e <env> -t upload --upload-port <port>` (or use the `pio_flash` MCP tool)                                                                                   |
| Run firmware unit tests (native) | `./bin/run-tests.sh` (preferred - ASan/LSan + RED/AMBER/GREEN verdict); or raw: `~/.platformio/penv/bin/python -m platformio test -e native > /tmp/test_out.txt 2>&1` |
| Run MCP hardware tests           | From a [meshtastic-mcp](https://github.com/meshtastic/meshtastic-mcp) checkout: `MESHTASTIC_FIRMWARE_ROOT=/path/to/firmware ./run-tests.sh`                           |
| Live TUI test runner             | `uvx --from git+https://github.com/meshtastic/meshtastic-mcp meshtastic-mcp-test-tui`                                                                                 |
| Format before commit             | `trunk fmt`                                                                                                                                                           |
| Regenerate protobuf bindings     | `bin/regen-protos.sh`                                                                                                                                                 |
| Generate CI matrix               | `./bin/generate_ci_matrix.py all [--level pr]`                                                                                                                        |

## MCP server (device + test automation)

The [meshtastic-mcp](https://github.com/meshtastic/meshtastic-mcp) server exposes ~32 MCP tools for device discovery, building, flashing, serial monitoring, and live-node administration. Tools are grouped as:

- **Discovery**: `list_devices`, `list_boards`, `get_board`
- **Build & flash**: `build`, `clean`, `pio_flash`, `erase_and_flash` (ESP32 factory), `update_flash` (ESP32 OTA), `touch_1200bps`
- **Serial sessions**: `serial_open`, `serial_read`, `serial_list`, `serial_close`
- **Device reads**: `device_info`, `list_nodes`
- **Device writes** (require `confirm=True`): `set_owner`, `get_config`, `set_config`, `get_channel_url`, `set_channel_url`, `send_text`, `reboot`, `shutdown`, `factory_reset`, `set_debug_log_api`
- **userPrefs admin**: `userprefs_get`, `userprefs_set`, `userprefs_reset`, `userprefs_manifest`, `userprefs_testing_profile`
- **Vendor escape hatches**: `esptool_*`, `nrfutil_*`, `picotool_*`

Setup: nothing to build - `.mcp.json` runs the server via `uvx --from git+https://github.com/meshtastic/meshtastic-mcp meshtastic-mcp`, so Claude Code picks it up automatically. To run the pytest hardware harness, clone [meshtastic-mcp](https://github.com/meshtastic/meshtastic-mcp) and set `MESHTASTIC_FIRMWARE_ROOT` to this firmware checkout.

See the meshtastic-mcp repo's README for argument shapes and the **MCP Server & Hardware Test Harness** section of `.github/copilot-instructions.md` for agent usage rules (tool surface, fixture contract, firmware integration points, recovery playbooks).

## Slash commands (AI-assisted workflows)

The test-and-diagnose workflows (`/test`, `/diagnose`, `/repro`, `/leakhunt`) now ship with the [meshtastic-mcp](https://github.com/meshtastic/meshtastic-mcp) repo as skills - run them from a checkout of that repo (with `MESHTASTIC_FIRMWARE_ROOT` pointed here). The MCP tools they build on are still available in this repo via `.mcp.json`.

## Encryption at a glance

Two layers, both in `src/mesh/CryptoEngine.cpp`:

- **Channel (symmetric)** - **AES-CTR** with a channel-wide PSK (AES-128 or AES-256). Nonce = packet_id ‖ from_node ‖ block_counter. No AEAD; integrity is soft (channel-hash filter). The well-known default PSK lives in `src/mesh/Channels.h`; a 1-byte PSK is a short-form index into it.
- **Per-peer PKI** - **X25519 ECDH** (Curve25519, 32-byte keys) → SHA-256 → **AES-256-CCM** with an 8-byte MAC. Fresh 32-bit `extraNonce` per packet, sent in the clear alongside the MAC. 12-byte wire overhead (`MESHTASTIC_PKC_OVERHEAD`). Used for DMs. Also used for remote admin (`src/modules/AdminModule.cpp`), where AdminMessage authorization is gated by `config.security.admin_key[0..2]`. Disabled entirely in Ham mode (`user.is_licensed=true`).

Key rotation to never trigger casually: only the **full** factory reset (`factory_reset_device`, `eraseBleBonds=true`) wipes `security.private_key` and regenerates the keypair - every peer holds the old public key, so DMs silently fail PKI decrypt until NodeInfo re-exchanges. The **partial** config reset (`factory_reset_config`) preserves the private key and doesn't invalidate peer relationships. Explicitly blanking `security.private_key` via admin also triggers regen. See the **Encryption & Key Management** section of `.github/copilot-instructions.md` for the full spec (nonce layout, send/receive selection logic including infrastructure-portnum exceptions, admin-key + session-passkey authorization, `is_managed` scope, key-rotation hazards).

## House rules

- **No destructive device operations without operator approval.** `factory_reset`, `erase_and_flash`, `reboot`, `shutdown`, history-rewriting git ops - describe the action and stop. Operator authorizes.
- **One MCP call per serial port at a time.** The port lock is exclusive; concurrent calls deadlock. Sequence: open → read/mutate → close, then next device.
- **`userPrefs.jsonc` is session state during tests.** The `_session_userprefs` fixture snapshots + restores it; never edit it from inside a test.
- **Don't speculate about firmware root causes.** When evidence doesn't support a classification, say "unknown" and list what would disambiguate.
- **Run `trunk fmt` before proposing a commit.** The `trunk_check` CI gate will reject unformatted code. Claude Code runs it automatically via the PostToolUse hook in `.claude/settings.json`; trunk's launcher needs `curl` or `wget` to bootstrap its pinned CLI - see **Formatting & the trunk toolchain** in `.github/copilot-instructions.md` for the no-curl bootstrap procedure.
- **Never edit or commit files under `src/mesh/generated/`.** They are regenerated from the [`meshtastic/protobufs`](https://github.com/meshtastic/protobufs) repo by the `update_protobufs.yml` workflow (entry point: `bin/regen-protos.sh`). Local edits will be overwritten and create merge conflicts. If a `.proto` change is needed, open a PR against the protobufs repo first, then let the workflow re-sync this repo.
- **`confirm=True` on destructive MCP tools is a real gate, not a formality.** Don't bypass it via auto-approve settings.
- **Keep code comments minimal - one or two lines, max.** Comment only when the _why_ isn't obvious from the code; never restate what the next line does. No multi-paragraph block comments explaining straightforward changes. The diff and commit message carry the rationale; the code carries the behavior.
- **Use `Throttle` for time-based rate limiting, not raw `millis()` math.** `src/mesh/Throttle.h` provides `Throttle::isWithinTimespanMs(lastMs, intervalMs)` (returns true while inside the cooldown) and `Throttle::execute(&lastMs, intervalMs, func)` (function-pointer form that updates the timestamp on fire). Use these for any "did N ms pass since X" check - raw `millis() > lastMs + N` is rollover-unsafe (breaks after ~49.7 days) and inconsistent with the rest of the codebase. The helpers compute `now - lastMs` with unsigned subtraction, which wraps correctly.

## Typical agent workflows

### Flashing a device

1. `list_devices` → find the port + likely VID
2. `list_boards` → confirm the env, or use the known default for the hardware
3. `pio_flash(env=..., port=..., confirm=True)` for any arch, or `erase_and_flash(env=..., port=..., confirm=True)` for an ESP32 factory install

### Inspecting live node state

1. `device_info(port=...)` - short summary (node num, firmware version, region, peer count)
2. `list_nodes(port=...)` - full peer table (SNR, RSSI, pubkey presence, last_heard)
3. `get_config(section="lora", port=...)` - LoRa settings for cross-device comparison

Sequence these; don't parallelize on the same port.

### Testing a firmware change

1. Build locally: `pio run -e <env>`
2. Flash the test device: `pio_flash(env=..., port=..., confirm=True)`
3. Run the suite from a meshtastic-mcp checkout: `MESHTASTIC_FIRMWARE_ROOT=/path/to/firmware ./run-tests.sh tests/<tier>` (or the `/test` skill)
4. On failure, open the run's `tests/report.html` → `Meshtastic debug` section for the firmware log tail + device state dump
5. Iterate

### Debugging a flaky test

1. `/repro <test-node-id> [count]` - re-runs the test N times, diffs firmware logs between passes and failures
2. If the first attempt always fails and the rest pass, that's a state-leak pattern → suggest `--force-bake` or a clean device state, don't chase the first failure
3. If all N fail, this isn't a flake - it's a regression. Stop iterating and escalate to `/test` for full-suite context.

## Where to look

| Path                                                           | What's there                                                                                                                                                                                   |
| -------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/`                                                         | Firmware C++ source (`mesh/`, `modules/`, `platform/`, `graphics/`, `gps/`, `motion/`, `mqtt/`, …)                                                                                             |
| `src/mesh/`                                                    | Core: NodeDB, Router, Channels, CryptoEngine, radio interfaces, StreamAPI, PhoneAPI                                                                                                            |
| `src/modules/`                                                 | Feature modules; `Telemetry/Sensor/` has 50+ I2C sensor drivers                                                                                                                                |
| `variants/`                                                    | 200+ hardware variant definitions (`variant.h` + `platformio.ini` per board)                                                                                                                   |
| `protobufs/`                                                   | `.proto` definitions; regenerate with `bin/regen-protos.sh`                                                                                                                                    |
| `test/`                                                        | Firmware unit tests (19 suites; `./bin/run-tests.sh` preferred, falls back to `pio test -e native`)                                                                                            |
| [meshtastic-mcp](https://github.com/meshtastic/meshtastic-mcp) | Standalone MCP server + tiered pytest hardware harness (`unit/`, `mesh/`, `telemetry/`, `monitor/`, `recovery/`, `ui/`, `fleet/`, `admin/`, `provisioning/`) - registered here via `.mcp.json` |
| `.github/prompts/`                                             | Copilot prompt bodies (firmware scaffolding: new module / sensor / variant)                                                                                                                    |
| `.github/copilot-instructions.md`                              | **Primary agent instructions - read this**                                                                                                                                                     |
| `.github/workflows/`                                           | CI pipelines                                                                                                                                                                                   |
| `.mcp.json`                                                    | MCP server registration for Claude Code                                                                                                                                                        |

## Recovery one-liners

- **`userPrefs.jsonc` dirty after a test run?** Re-run the meshtastic-mcp harness once (pre-flight self-heals from the sidecar). If still dirty: `git checkout userPrefs.jsonc`.
- **nRF52 not responding?** `mcp__meshtastic__touch_1200bps(port=...)` drops it into the DFU bootloader, then `pio_flash` re-installs.
- **Device fully wedged (no DFU)?** `mcp__meshtastic__uhubctl_cycle(role="nrf52", confirm=True)` hard-power-cycles it via USB hub PPPS. Needs `uhubctl` installed (`brew install uhubctl` / `apt install uhubctl`); on Linux without udev rules, permission errors fail fast, so use `sudo uhubctl` yourself or configure udev access.
- **Port busy?** `lsof <port>` to find the holder. Usually a stale `pio device monitor` or zombie `meshtastic_mcp` process. Kill it.
- **Multiple MCP servers running?** `ps aux | grep meshtastic_mcp` - zombies hold ports. Kill all but the one your host spawned.
- **macOS: `LIBUSB_ERROR_BUSY` on a CH341 LoRa adapter?** A third-party WCH `CH34xVCPDriver` is claiming interface 0. Find the bundle ID with `ioreg -p IOUSB -l -w 0 | grep -B2 -A30 0x5512`, then `sudo kmutil unload -b <bundleID>`. Apple's bundled CH34x kext targets the CH340 UART (PID 0x7523), not the SPI bridge - it's never the culprit.

## Environment variables (test harness)

| Var                                  | Purpose                                                                                                                                                                                                    |
| ------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `MESHTASTIC_MCP_ENV_<ROLE>`          | Override PlatformIO env for a role (e.g. `MESHTASTIC_MCP_ENV_NRF52=rak4631-dap`). Default map: `nrf52→rak4631`, `esp32s3→heltec-v3`.                                                                       |
| `MESHTASTIC_MCP_SEED`                | PSK seed for the session test profile. Defaults to `mcp-<user>-<host>`.                                                                                                                                    |
| `MESHTASTIC_MCP_FLASH_LOG`           | File path to tee pio/esptool/nrfutil/picotool output. `run-tests.sh` sets this to `tests/flash.log` so the TUI can stream live flash progress.                                                             |
| `MESHTASTIC_MCP_TCP_HOST`            | `host` or `host:port` of a `meshtasticd` daemon (e.g. the `native-macos` build). Surfaces it in `list_devices` as `tcp://host:port` so `connect()`-based tools target it transparently. Default port 4403. |
| `MESHTASTIC_UHUBCTL_BIN`             | Absolute path to `uhubctl` binary. Default: PATH lookup.                                                                                                                                                   |
| `MESHTASTIC_UHUBCTL_LOCATION_<ROLE>` | Pin a role to a specific uhubctl hub location (e.g. `1-1.3`). Wins over VID auto-detection - use when multiple devices share a VID.                                                                        |
| `MESHTASTIC_UHUBCTL_PORT_<ROLE>`     | Pin a role to a specific hub port number. Required alongside `LOCATION_<ROLE>`.                                                                                                                            |
| `MESHTASTIC_UI_CAMERA_BACKEND`       | Camera backend for UI tier + `capture_screen` tool: `opencv` / `ffmpeg` / `null` / `auto` (default).                                                                                                       |
| `MESHTASTIC_UI_CAMERA_DEVICE`        | Generic camera device (index or path). Used by the UI tier when no per-role var is set.                                                                                                                    |
| `MESHTASTIC_UI_CAMERA_DEVICE_<ROLE>` | Per-role camera pinning (e.g. `MESHTASTIC_UI_CAMERA_DEVICE_ESP32S3=0` for the OLED-bearing heltec-v3).                                                                                                     |
| `MESHTASTIC_UI_OCR_BACKEND`          | OCR engine selection: `easyocr` / `pytesseract` / `null` / `auto` (default).                                                                                                                               |
| `MESHTASTIC_UI_TUI_CAMERA`           | Set to `1` to mount the live camera-feed panel in `meshtastic-mcp-test-tui`.                                                                                                                               |
