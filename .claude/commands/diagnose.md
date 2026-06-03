---
description: Produce a device health report using the meshtastic MCP tools (device_info, list_nodes, get_config, short serial log capture)
argument-hint: [role=all|nrf52|esp32s3|<port>]
---

# `/diagnose` — device health report

Call the meshtastic MCP tool bundle and format a structured health report for one or all detected devices. Zero guesswork for the operator.

## What to do

1. **Enumerate hardware.** Call `mcp__meshtastic__list_devices(include_unknown=True)`. For each entry where `likely_meshtastic=True`, capture `port`, `vid`, `pid`, `description`.

2. **Filter by `$ARGUMENTS`**:
   - No args, `all` → every likely-meshtastic device.
   - `nrf52` → only devices with `vid == 0x239a`.
   - `esp32s3` → only devices with `vid == 0x303a` or `vid == 0x10c4`.
   - A `/dev/cu.*` path → only that one port.
   - Anything else → treat as a substring match against the `port` string.

3. **For each selected device, in sequence (NOT parallel — SerialInterface holds an exclusive port lock):**
   - `mcp__meshtastic__device_info(port=<p>)` — captures `my_node_num`, `long_name`, `short_name`, `firmware_version`, `hw_model`, `region`, `num_nodes`, `primary_channel`.
   - `mcp__meshtastic__list_nodes(port=<p>)` — count of peers, which ones have `publicKey` set, SNR/RSSI distribution.
   - `mcp__meshtastic__get_config(section="lora", port=<p>)` — region, preset, channel_num, tx_power, hop_limit.
   - Optionally, if the device seems unhappy (fails to connect, `num_nodes==1` when ≥2 are plugged in, missing firmware*version), open a short firmware log window: `mcp__meshtastic__serial_open(port=<p>, env=<inferred-env>)`, wait 3s, `serial_read(session_id=<s>, max_lines=100)`, `serial_close(session_id=<s>)`. The env should be inferred from the VID map in `mcp-server/run-tests.sh` (nrf52 → rak4631, esp32s3 → heltec-v3) unless `MESHTASTIC_MCP_ENV*<ROLE>` is set.

4. **Hub health** (call once, not per-device): `mcp__meshtastic__uhubctl_list()` — enumerates every USB hub the host can see. Note which hubs advertise `ppps=true` and which hub hosts each Meshtastic device (cross-reference by VID). Flag it in the report if:
   - No hub advertises PPPS → `tests/recovery/` can't run on this setup; hard-recovery via `uhubctl_cycle` isn't available.
   - A Meshtastic device is on a non-PPPS hub → note it; operator may want to move the device to a PPPS hub to unlock auto-recovery.
   - `uhubctl_list` raises `ConfigError: uhubctl not found` → just say `uhubctl not installed` in the report; don't treat as a fault.

5. **Render per-device report** as:

   ```text
   [nrf52 @ /dev/cu.usbmodem1101]      fw=2.7.23.bce2825, hw=RAK4631
     owner       : Meshtastic 40eb / 40eb
     region/band : US, channel 88, LONG_FAST
     tx_power    : 30 dBm, hop_limit=3
     peers       : 1 (esp32s3 0x433c2428, pubkey ✓, SNR 6.0 / RSSI -24 dBm)
     primary ch  : McpTest
     hub         : 1-1.3 port 2 (PPPS, uhubctl-controllable)
     firmware    : no panics in last 3s; NodeInfoModule emitted 2 broadcasts
   ```

   Keep it scannable. If a field is missing or abnormal (no pubkey for a known peer, region=UNSET, num_nodes inconsistent with the hub, device on non-PPPS hub), flag it inline with a short `⚠︎ <one-line reason>`.

6. **Cross-device correlation** (only when >1 device is inspected):
   - Do both sides see each other in `nodesByNum`? If one does and the other doesn't, that's asymmetric NodeInfo — flag it.
   - Do the LoRa configs match? (region, channel_num, modem_preset should all agree; mismatch = no mesh)
   - Do the primary channel NAMES match? Mismatch = different PSK = no decode.

7. **Recorder slice (cheap, always available).** The mcp-server runs an autouse log recorder that's been collecting from every connected device. Pull two short slices to surface anything weird that's already happened:
   - `mcp__meshtastic__logs_window(start="-2m", level="WARN|ERROR|CRIT", max_lines=20)` — recent firmware errors. If empty, say "no recent errors"; don't manufacture concern.
   - `mcp__meshtastic__telemetry_timeline(window="1h", field="free_heap", max_points=60)` — heap trend. If `slope_per_min < -50`, flag it and recommend `/leakhunt window=6h` for a deeper read; otherwise just note the current free heap.
   - If `recorder_status` shows `running:false` or `files.telemetry.last_ts` is null, note "recorder has no telemetry yet — enable `set_debug_log_api(True)` to populate" and skip this step gracefully.

8. **Suggest next actions only for specific, recognisable failure modes**:
   - Stale PKI pubkey one-way → "run `/test tests/mesh/test_direct_with_ack.py` — the retry + nodeinfo-ping heals this in the test path."
   - Region mismatch → "re-bake one side via `./mcp-server/run-tests.sh --force-bake`."
   - Device unreachable, reachable via DFU → `touch_1200bps(port=...)` + `pio_flash`. If not even DFU responds AND the device is on a PPPS hub, escalate to `uhubctl_cycle(role=..., confirm=True)`.
   - CP2102-wedged-driver on macOS → see the note in `run-tests.sh`.
   - Heap slope strongly negative → "run `/leakhunt window=6h` for a full timeline + classification."

## What NOT to do

- No writes. No `set_config`, no `reboot`, no `factory_reset`. This is a read-only diagnostic skill — if the operator wants to change state, they'll ask explicitly.
- No `flash` / `erase_and_flash`. Those are separate escalations.
- No holding SerialInterface across tool calls — open, query, close; next device. The port lock is exclusive.
