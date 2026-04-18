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

4. **Render per-device report** as:

   ```text
   [nrf52 @ /dev/cu.usbmodem1101]      fw=2.7.23.bce2825, hw=RAK4631
     owner       : Meshtastic 40eb / 40eb
     region/band : US, channel 88, LONG_FAST
     tx_power    : 30 dBm, hop_limit=3
     peers       : 1 (esp32s3 0x433c2428, pubkey ✓, SNR 6.0 / RSSI -24 dBm)
     primary ch  : McpTest
     firmware    : no panics in last 3s; NodeInfoModule emitted 2 broadcasts
   ```

   Keep it scannable. If a field is missing or abnormal (no pubkey for a known peer, region=UNSET, num_nodes inconsistent with the hub), flag it inline with a short `⚠︎ <one-line reason>`.

5. **Cross-device correlation** (only when >1 device is inspected):
   - Do both sides see each other in `nodesByNum`? If one does and the other doesn't, that's asymmetric NodeInfo — flag it.
   - Do the LoRa configs match? (region, channel_num, modem_preset should all agree; mismatch = no mesh)
   - Do the primary channel NAMES match? Mismatch = different PSK = no decode.

6. **Suggest next actions only for specific, recognisable failure modes**:
   - Stale PKI pubkey one-way → "run `/test tests/mesh/test_direct_with_ack.py` — the retry + nodeinfo-ping heals this in the test path."
   - Region mismatch → "re-bake one side via `./mcp-server/run-tests.sh --force-bake`."
   - Device unreachable → point at touch_1200bps + the CP2102-wedged-driver note in run-tests.sh.

## What NOT to do

- No writes. No `set_config`, no `reboot`, no `factory_reset`. This is a read-only diagnostic skill — if the operator wants to change state, they'll ask explicitly.
- No `flash` / `erase_and_flash`. Those are separate escalations.
- No holding SerialInterface across tool calls — open, query, close; next device. The port lock is exclusive.
