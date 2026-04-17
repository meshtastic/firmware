---
mode: agent
description: Device health report via the meshtastic MCP tools (Copilot equivalent of the Claude Code /diagnose slash command)
---

# `/mcp-diagnose` — device health report

Equivalent of `.claude/commands/diagnose.md`. Use when the operator asks to "check the devices", "what's the mesh looking like", "is nrf52 alive", etc.

This prompt assumes the meshtastic MCP server is registered with your VS Code Copilot agent. If it isn't, fall back to running `./mcp-server/run-tests.sh tests/unit` plus a short `device_info` script via the terminal.

## What to do

1. **Enumerate hardware** via the `list_devices` MCP tool (with `include_unknown=True`). For each entry where `likely_meshtastic=True`, capture `port`, `vid`, `pid`, `description`.

2. **Apply the operator's filter** (if any):
   - No filter → every likely-meshtastic device.
   - `nrf52` → `vid == 0x239a`
   - `esp32s3` → `vid == 0x303a` or `vid == 0x10c4`
   - A `/dev/cu.*` path → only that port.
   - Anything else → substring match on port.

3. **For each selected device, in sequence (don't parallelize — SerialInterface holds an exclusive port lock):**
   - `device_info(port=<p>)` → `my_node_num`, `long_name`, `short_name`, `firmware_version`, `hw_model`, `region`, `num_nodes`, `primary_channel`
   - `list_nodes(port=<p>)` → peer count, which peers have `publicKey`, SNR/RSSI distribution
   - `get_config(section="lora", port=<p>)` → region, preset, channel_num, tx_power, hop_limit
   - If anything looks off (can't connect, `num_nodes` wrong, missing `firmware_version`), open a short firmware-log window: `serial_open(port=<p>, env=<inferred>)`, wait 3 seconds, `serial_read(session_id, max_lines=100)`, `serial_close(session_id)`. Infer env from VID (0x239a → `rak4631`, 0x303a/0x10c4 → `heltec-v3`) unless an `MESHTASTIC_MCP_ENV_<ROLE>` env var overrides it.

4. **Render per-device report** as a compact block:

   ```
   [nrf52 @ /dev/cu.usbmodem1101]      fw=2.7.23.bce2825, hw=RAK4631
     owner       : Meshtastic 40eb / 40eb
     region/band : US, channel 88, LONG_FAST
     tx_power    : 30 dBm, hop_limit=3
     peers       : 1 (esp32s3 0x433c2428, pubkey ✓, SNR 6.0 / RSSI -24 dBm)
     primary ch  : McpTest
     firmware    : no panics in last 3s
   ```

   Flag abnormalities inline with `⚠︎ <short reason>` — missing pubkey on a known peer, region UNSET, mismatched channel name, etc.

5. **Cross-device correlation** (when >1 device selected):
   - Do both see each other in `nodesByNum`?
   - Do `region`, `channel_num`, `modem_preset` match across devices?
   - Do the primary channel names match? (Different name → different PSK → no decode.)

6. **Suggest next steps only for recognizable failure modes**, never speculatively:
   - Stale PKI one-way → "`/mcp-test tests/mesh/test_direct_with_ack.py` — the test's retry+nodeinfo-ping heals this."
   - Region mismatch → "re-bake one side via `./mcp-server/run-tests.sh --force-bake`."
   - Device unreachable → refer operator to the touch_1200bps + CP2102-wedged-driver notes in `run-tests.sh`.

## Hard constraints

- **Read-only.** No `set_config`, no `reboot`, no `factory_reset`, no `flash`. If the operator wants mutation, they'll escalate explicitly.
- **Open/query/close per device.** Never hold multiple SerialInterfaces to the same port. The port lock is exclusive.
- **Don't infer env beyond the VID map** — if the operator has an unusual board, ask them which env to use rather than guessing.
