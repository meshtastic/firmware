# AdminModule config-save side effects: radio-reload & reboot gating

**Status:** Implemented
**Date:** 2026-07-23
**Area:** `src/modules/AdminModule.cpp` (`handleSetConfig`, `saveChanges`), `src/mesh/MeshService.cpp` (`reloadConfig`), `src/mesh/NodeDB.h` (`SEGMENT_*`)
**Commits:** `babeef08d` (radio-reload), `65c27f813` + `273fa8d0a` (segment docs / TODO markers), `71d711867` (reboot Tier 1), `cc9abdbbc` (reboot Tier 2)

Saving a config over the phone/BLE used to do more than persist bytes: it could re-init the
LoRa radio **and** reboot the device, on config that touched neither. This document records
what those saves do now, which operations were deliberately left alone, and why.

---

## TL;DR

A config-set admin message carries three _independent_ axes that had become conflated:

1. **What to persist** - the `saveWhat` segment bitmask (which on-disk file to rewrite).
2. **Does the LoRa radio need reconfiguring?** - now the explicit `radioAffected` flag.
3. **Does the device need a reboot?** - the `requiresReboot` flag.

Previously (2) was _inferred_ from (1) (`saveWhat & SEGMENT_CONFIG`), and (3) defaulted to
`true` for several sub-messages that never revisited it. Both inferences were wrong for
config that doesn't touch LoRa or doesn't need a restart. They are now decided explicitly and
per-field. Net effect: toggling Bluetooth, changing a WiFi PSK, rotating keys, or nudging a
position broadcast interval no longer re-inits the radio, and several of them no longer reboot.

The guiding rule for what was left alone: **fail toward the old behavior.** Anything whose
live-apply couldn't be _proven_ safe still reloads/reboots exactly as before.

---

## Background: what a segment is

`SEGMENT_*` ([`NodeDB.h:87`](../src/mesh/NodeDB.h#L87)) is a bit selecting **one on-disk proto
file - a write unit, not a semantic config domain.** `SEGMENT_CONFIG` is the _entire_
`LocalConfig` proto in a single file, covering all eight sub-messages
(device/display/lora/position/power/network/bluetooth/security). Writing it reserializes the
whole struct, so the mask can say _which file_ changed but never _which sub-message_ - it
cannot tell a LoRa change from a Bluetooth one. That is precisely why "does the radio need
reconfiguring?" cannot be inferred from the mask and needs its own signal.

---

## Axis 1 - Radio reload (`radioAffected`)

`MeshService::reloadConfig(int saveWhat, bool radioAffected = true)`
([`MeshService.cpp:138`](../src/mesh/MeshService.cpp#L138)) fires the live SX126x reconfigure
(`resetRadioConfig()` + `configChanged.notifyObservers()`) only when:

```cpp
if (radioAffected && (saveWhat & (SEGMENT_CONFIG | SEGMENT_CHANNELS)))
```

The bitmask gate is **retained** (so the ~35 non-AdminModule callers are unaffected) and
`radioAffected` only ever _suppresses_ the reload. It defaults to `true`, so any caller that
does not opt out keeps the historical behavior. `AdminModule::saveChanges`
([`:1846`](../src/modules/AdminModule.cpp#L1846)) threads it through; `handleSetConfig`
([`:836`](../src/modules/AdminModule.cpp#L836)) sets `radioAffected = true`
([`:845`](../src/modules/AdminModule.cpp#L845)) only in the `lora_tag` case.

| Admin operation                                                         | Reloads radio now? | Notes                                                                                                    |
| ----------------------------------------------------------------------- | ------------------ | -------------------------------------------------------------------------------------------------------- |
| `set_config` → `lora`                                                   | **Yes**            | The only config that feeds `RadioInterface::reconfigure()` (reads `config.lora`). Region swaps included. |
| `set_config` → device/position/power/network/display/bluetooth/security | **No** (was yes)   | Persist `SEGMENT_CONFIG`, but none affect the modem.                                                     |
| `set_fixed_position` / `remove_fixed_position`                          | **No** (was yes)   | Rode `SEGMENT_CONFIG` only because `fixed_position` shares the Config file.                              |
| `set_channel`                                                           | **Yes**            | Real frequency-slot/PSK change ([`:1445`](../src/modules/AdminModule.cpp#L1445)).                        |

Why this matters beyond tidiness: the reconfigure is a live `setStandby()` + SPI reprogram run
from the admin-message handler on the main task, while the radio is active - the exact
sequence that crashed the WisMesh Tag on a favorite (#11146). Removing it for non-LoRa saves
closes the rest of that crash class and
avoids a needless RX gap on the mesh radio.

---

## Axis 2 - Device reboot (`requiresReboot`)

`handleSetConfig` starts `requiresReboot = true` ([`:841`](../src/modules/AdminModule.cpp#L841))
and reboots via `saveChanges` when it stays true. `device`, `power`, and `display` already
narrowed it (reboot only when a reboot-relevant field changed); `position`, `network`, and
`bluetooth` never did - they rebooted on **every** set, including a client re-pushing
byte-identical config. Two tiers fixed that.

### Tier 1 - no-op gate (position / network / bluetooth)

If the incoming sub-message is byte-identical to the current one, skip the reboot. Uses a
whole-struct `memcmp` - the right tool for "did anything change?", and fail-safe: its only
error mode is padding bytes differing → an _unnecessary_ reboot (old behavior), never a
missed change. All three sub-messages are POD (no `pb_callback_t`).

- `network` ([`:952`](../src/modules/AdminModule.cpp#L952)) and `bluetooth`
  ([`:1136`](../src/modules/AdminModule.cpp#L1136)): any **real** change still reboots
  (restarts the WiFi/eth or BLE stack).

### Tier 2 - position live-apply

`position` ([`:901`](../src/modules/AdminModule.cpp#L901)) reboots only when a field that
**cannot** be applied live changed. The live set is exactly what `PositionModule` reads
directly from `config` each send/schedule cycle:

| Field                                     | Reboot on change? | Rationale                                         |
| ----------------------------------------- | ----------------- | ------------------------------------------------- |
| `position_broadcast_secs`                 | No                | Read live in the broadcast scheduler              |
| `position_broadcast_smart_enabled`        | No                | Read live in the send path                        |
| `broadcast_smart_minimum_distance`        | No                | Read live in the smart-position path              |
| `position_flags`                          | No                | Read live per outgoing position                   |
| `fixed_position`                          | No                | Read live; also has dedicated live admin handlers |
| `gps_mode`, `gps_enabled`                 | **Yes**           | GPS driver state                                  |
| `gps_update_interval`, `gps_attempt_time` | **Yes**           | GPS subsystem timing                              |
| `rx_gpio`, `tx_gpio`, `gps_en_gpio`       | **Yes**           | GPIO pin (re)assignment                           |

The gate neutralizes the live fields in a copy and reboots if any _other_ byte differs, so a
newly-added `PositionConfig` field reboots until it is explicitly cleared as live - fail-safe
for schema growth.

---

## Operations intentionally left unchanged ("third tier")

These were audited and deliberately **not** narrowed. Each still reloads and/or reboots as
before. The common thread: the reload/reboot is either genuinely required, or removing it
would need real state-tracking / hardware verification that carries more risk than the saving
is worth.

1. **`commit_edit_settings`** ([`:474`](../src/modules/AdminModule.cpp#L474)) - commits a
   whole edit transaction with a fixed
   `SEGMENT_CONFIG | SEGMENT_MODULECONFIG | SEGMENT_DEVICESTATE | SEGMENT_CHANNELS | SEGMENT_NODEDATABASE`
   save (default `radioAffected=true`, `shouldReboot=true`), so it reloads and reboots on
   **every** commit regardless of what was actually edited.
   **Why untouched:** narrowing it requires the transaction to track _which_ segments were
   dirtied (and whether LoRa/reboot-relevant fields were among them) as individual sets ran
   under `hasOpenEditTransaction`, then OR them at commit - a real state-tracking change, not
   a one-line gate. Risk is also lower here: edit transactions are rarely
   node-DB-metadata-only, so the wasteful case is less common. Deferred to its own pass; the
   same tracking would let it decide both `radioAffected` and `requiresReboot`.

2. **`network` / `bluetooth` beyond the Tier-1 no-op gate** - a _real_ change still reboots.
   **Why untouched:** these restart the WiFi/Ethernet and BLE stacks. Applying a live change
   to a partially-initialized stack is genuinely hard to get right and high-risk (dropped
   links, half-configured interfaces) for little reward. The safe, cheap win (skip the reboot
   on no-op re-pushes) was taken; live-apply was not.

3. **GPS-timing position fields** (`gps_mode`, `gps_enabled`, `gps_update_interval`,
   `gps_attempt_time`) - still reboot.
   **Why untouched:** they touch the GPS subsystem/driver and could not be _statically_
   proven to apply live (unlike the cadence/flag fields, which are read straight from
   `config` each cycle). Following the fail-toward-reboot rule, they stay on the reboot path.
   Moving them to the live set is exactly what a hardware verification pass is for - confirm
   on a real node that the new value takes effect with no restart, then reclassify.

4. **`handleSetModuleConfig`** ([`:1215`](../src/modules/AdminModule.cpp#L1215)) - every
   module-config set reboots (`shouldReboot = true`).
   **Why untouched:** most module sets start or stop background threads (telemetry, serial,
   MQTT, store-and-forward, detection sensor), where a reboot is genuinely warranted.
   Separating the few that plausibly don't need one (canned messages, ambient lighting,
   status message) needs a per-module audit - its own effort, not part of this work.

5. **On-device menu `reloadConfig` sites** - 27 `reloadConfig(SEGMENT_CONFIG)` /
   `reloadConfig(changes)` calls in `src/graphics/draw/MenuHandler.cpp` and
   `src/graphics/niche/InkHUD/Applets/System/Menu/MenuApplet.cpp` still reload on any Config
   save (they pass only `saveWhat`; the default `radioAffected=true` preserves this).
   **Why untouched:** this work was scoped to the AdminModule (client admin-message) crash class.
   The menu paths are a mix of genuinely-radio changes (region/preset) and incidental ones
   (role, display units), run on the UI thread. Each is marked with a
   `// TODO(radioAffected)` note (`audit` vs `radio-affecting`) for a future per-site pass.

For contrast, `set_channel` ([`:1445`](../src/modules/AdminModule.cpp#L1445)) and
`restore_preferences` ([`:1911`](../src/modules/AdminModule.cpp#L1911)) also still reload -
but that is _correct_, not conservative: both genuinely change LoRa/channel state.

---

## Verification

Native coverage lives in `test/test_admin_radio/test_main.cpp`:

- **Radio reload:** each non-LoRa `set_config` sub-message asserts `configChanged` does **not**
  fire (`ConfigChangedCounter`); `lora` and `set_channel` assert it still does; direct
  `reloadConfig` guards pin the fail-safe default and the explicit opt-out.
- **Reboot:** each of position/network/bluetooth asserts `rebootAtMsec` stays `0` on a no-op
  set and is armed on a real change; a position broadcast-interval change asserts **no**
  reboot, while `gps_mode` and `rx_gpio` changes assert a reboot.

The harness defers `reboot()` to `rebootAtMsec` (it does not exit), which is the signal the
reboot tests assert on.

Native tests cannot observe the actual side effects - the live SX126x SPI sequence and a real
reboot don't happen in the host build - so the two claims that matter most (the crash is gone;
"live" fields really apply without a restart) require hardware. See below.

---

## Hardware testing

Run against a real node through the
[meshtastic-mcp](https://github.com/meshtastic/meshtastic-mcp) harness
(`MESHTASTIC_FIRMWARE_ROOT` → this checkout), with the serial log open. A nRF52840 SX126x
board (e.g. WisMesh Tag) is the reference target; the crash was reproduced there.

**Transport: serial is sufficient - the on-device menus are not needed, and neither is BLE.**
These operations are all client-protocol admin messages (`ToRadio`), carried identically over
serial (`SerialConsole`/`StreamAPI`) or BLE; the on-device button/screen menus are a separate
code path this work did not touch. Crucially, on nRF52 the BLE `onWrite` callback only
_queues_ the packet - `handleToRadio` → `saveChanges` → `reloadConfig` (the reconfigure) runs
on the **main FreeRTOS task** ([`NimbleBluetooth.cpp:135`](../src/nimble/NimbleBluetooth.cpp#L135)),
exactly where `SerialConsole` (an `OSThread`) services the serial stream. So the code and
thread under test are the same whichever transport you use, and the original crash was
serial-proven. Drive the admin messages over USB serial (e.g. the `meshtastic --port` CLI);
use BLE only if you specifically want to reproduce the exact user-facing conditions.

### 1. Radio-reload / crash validation (regression guard for `babeef08d`)

Confirms the non-LoRa config saves no longer run the live radio reconfigure - the
`setStandby()` + SPI reprogram (on the main task, while the radio is active) that rebooted the
WisMesh Tag on a favorite (#11146).

Perform each of: toggle Bluetooth `enabled`, change the WiFi PSK, rotate the security
keypair, favorite/unfavorite a node.

- **Pass:** no radio re-init in the serial log before any reboot (no modem-reconfigure /
  `setStandby` / re-`init` lines), and no watchdog reboot-loop. Saves that legitimately
  reboot (WiFi, keypair) do so _cleanly, after_ the save - the reboot is fine; a live
  reconfigure _before_ it is the failure.
- **Positive control:** a `lora` config change and a `set_channel` **should** show the
  reconfigure - proves the path still fires when it should.

### 2. Position live-apply - expanding the Tier-2 live set (outstanding)

The only reason to touch a node for the _reboot_ work: decide whether the GPS-timing fields
left on the reboot path (`gps_mode`, `gps_enabled`, `gps_update_interval`, `gps_attempt_time`

- item 3 above) can actually apply live and be reclassified.

For each candidate field, one at a time, on a GPS-equipped node: change only that field (over
serial, as above) and observe.

- **Pass (→ reclassify as live):** the new value takes visible effect (e.g. GPS poll cadence
  changes, mode switches) **and** no reboot banner appears. Only then add the field to the
  live set in the `position_tag` gate and a native no-reboot case.
- **Fail (→ leave rebooting):** the value only takes effect after a restart, or the node
  reboots. This is the expected default - **fail toward rebooting**; do not reclassify on
  native evidence alone.

Fields already shipped as live (`position_broadcast_secs`, `position_broadcast_smart_enabled`,
`broadcast_smart_minimum_distance`, `position_flags`, `fixed_position`) were proven live by
static analysis; a spot-check that a broadcast-interval change takes effect with no reboot is
a cheap confidence test but not a gate.

---

## Extending this safely

- To stop a config field from reloading the radio: it already doesn't, unless it's `lora`.
  Do **not** widen `radioAffected` to non-LoRa config - only `RadioInterface::reconfigure()`
  (which reads `config.lora`) consumes it.
- To move a field off the reboot path: confirm it is consumed live (read from `config` at use
  time, no cached/driver state), add it to the live set in the relevant `handleSetConfig`
  case, and add a native case asserting no reboot on its change. For anything driver- or
  hardware-backed, verify on real hardware first. When unsure, leave it rebooting.
