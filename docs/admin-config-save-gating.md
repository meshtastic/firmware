# AdminModule config-save side effects: radio-reload & reboot gating

**Status:** Implementation complete; hardware validation outstanding
**Date:** 2026-07-23
**Area:** `src/modules/AdminModule.cpp` (`handleSetConfig`, `saveChanges`), `src/mesh/MeshService.cpp` (`reloadConfig`), `src/mesh/NodeDB.h` (`SEGMENT_*`)
**Reference:** meshtastic/firmware#11181 (commit SHAs omitted - they do not survive rebase or squash merge)

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
`radioAffected` only ever _suppresses_ the reload. `MeshService::reloadConfig` still defaults it
to `true`, so any external caller that does not opt out keeps the historical behavior.

`AdminModule::saveChanges(saveWhat, shouldReboot, radioAffected)` threads it through, and there it
has **no default** - every one of its call sites states the answer. That is not stylistic: see
"Edit transactions" below for why an inherited `true` stops being harmless once a transaction is
open. `handleSetConfig` sets `radioAffected = true` only in the `lora_tag` case.

| Admin operation                                                         | Reloads radio now? | Notes                                                                                                    |
| ----------------------------------------------------------------------- | ------------------ | -------------------------------------------------------------------------------------------------------- |
| `set_config` → `lora`                                                   | **Yes**            | The only config that feeds `RadioInterface::reconfigure()` (reads `config.lora`). Region swaps included. |
| `set_config` → device/position/power/network/display/bluetooth/security | **No** (was yes)   | Persist `SEGMENT_CONFIG`, but none affect the modem.                                                     |
| `set_fixed_position` / `remove_fixed_position`                          | **No** (was yes)   | Rode `SEGMENT_CONFIG` only because `fixed_position` shares the Config file.                              |
| `set_module_config`                                                     | **No**             | No module config feeds `reconfigure()`. Previously only the segment mask stopped it.                     |
| `set_owner`                                                             | **No**             | Node metadata. Still reboots (pre-existing) - that axis was left alone.                                  |
| favorite / ignore / mute node                                           | **No**             | Node-DB bits. The #11146 crash path.                                                                     |
| `set_channel`                                                           | **Yes**            | Real frequency-slot/PSK change.                                                                          |
| `set_ham_mode`                                                          | **Yes**            | Rewrites `config.lora.tx_enabled` and strips channel PSKs.                                               |

Why this matters beyond tidiness: the reconfigure is a live `setStandby()` + SPI reprogram. It
is _invoked_ from the admin-message handler on the main task (BLE `onWrite` only queues, so
this is not the BLE callback thread), but the failure is cross-thread: the radio runs its own
off-main worker (`RadioLibInterface`, a `NotifiedWorkerThread`) that also drives SPI under the
shared, **non-recursive** `spiLock` (a FreeRTOS binary semaphore). The main-task reconfigure
colliding with that worker over the lock / radio state locks the worker up, and the watchdog
reboots the device - the crash seen on the WisMesh Tag favorite (#11146; same non-recursive
`spiLock` hazard as #10705/#10728). Removing the reconfigure for non-LoRa saves closes the
rest of that crash class and
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

## Edit transactions - where both axes nearly got lost

Read this before adding a `saveChanges()` call site.

A client may bracket a run of admin messages in `begin_edit_settings` / `commit_edit_settings`.
While the transaction is open, `saveChanges()` **defers**: it writes nothing and returns, so a
multi-field set doesn't hit flash once per field. The commit then performs a single save under a
**fixed full-segment mask**.

Two consequences, both easy to miss:

1. **The per-field decisions had nowhere to go.** Everything the two axes above compute happens
   inside `handleSetConfig`, whose `saveChanges()` call is the one being deferred. The commit's
   own `saveChanges()` originally passed nothing, so it took the parameter defaults - reboot and
   reconfigure, on every commit, whatever was edited. Phone apps write config through
   transactions, so **none of the narrowing above reached them.** `deferredShouldReboot` and
   `deferredRadioAffected` now accumulate (`|=`) the deferred answers; the commit consumes them
   and clears them, so a stray second commit cannot inherit the previous transaction's answer.

2. **The segment bitmask stops protecting you.** Outside a transaction, a node-DB-only save
   physically cannot reach the radio: `saveWhat & (SEGMENT_CONFIG | SEGMENT_CHANNELS)` is zero
   whatever `radioAffected` says. Inside one, the commit saves under the full mask, so that gate
   always passes and `radioAffected` becomes the _only_ thing deciding it. An accidental `true` -
   inherited from a default, on a call site that never thought about the radio - then reaches the
   live SX126x reconfigure at commit. Favoriting a node from the phone app would have gone
   straight back through the #11146 crash path.

   That is why `AdminModule::saveChanges` gives `radioAffected` **no default**. Do not add one
   back. The nested `saveChanges()` inside the `position_tag` case is the subtle instance: an
   inner call on an unrelated segment can opt the whole transaction in.

Covered natively: see the "Edit-transaction deferral" block in `test_admin_radio`, which asserts
both axes independently across a commit (node-DB, module-config and nested-position batches
reconfigure nothing; a LoRa batch still does; a GPS-mode batch reboots without reconfiguring).

---

## Operations intentionally left unchanged ("third tier")

These were audited and deliberately **not** narrowed. Each still reloads and/or reboots as
before. The common thread: the reload/reboot is either genuinely required, or removing it
would need real state-tracking / hardware verification that carries more risk than the saving
is worth.

1. ~~**`commit_edit_settings`**~~ - **now done, see "Edit transactions" below.** Initially
   deferred to its own pass (it needed real state-tracking rather than a one-line gate), then
   done in the same PR once it became clear the deferral was discarding every decision the rest
   of this work computes.

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

5. ~~**On-device menu `reloadConfig` sites**~~ - **now done, see "On-device menus" below.**
   These were initially left alone as out of scope, then migrated in the same PR.

For contrast, `set_channel` ([`:1445`](../src/modules/AdminModule.cpp#L1445)) and
`restore_preferences` ([`:1911`](../src/modules/AdminModule.cpp#L1911)) also still reload -
but that is _correct_, not conservative: both genuinely change LoRa/channel state.

---

## On-device menus (`applyConfigChange`)

Both UIs go through one entry point on `MeshService`, so a menu action states the same two
decisions AdminModule does rather than inheriting defaults:

```cpp
void applyConfigChange(int saveWhat, uint8_t flags, int32_t rebootSeconds = DEFAULT_REBOOT_SECONDS);

enum ConfigApplyFlags : uint8_t {
    CONFIG_APPLY_NONE = 0,        // persist only
    CONFIG_APPLY_RADIO = 1 << 0,  // region/preset/freq/channel/PSK - re-init the LoRa chip
    CONFIG_APPLY_REBOOT = 1 << 1, // field only takes effect after a restart
};
```

`flags` has no default: the point is that every call site says what it wants. A flags enum
rather than two bools because `reboot` and `radioAffected` are both bools, sat in different
positions across the helpers this replaced, and transposing them compiled silently. InkHUD's
`applyConfigReload(changes, reboot)` took `reboot` exactly where `reloadConfig` and
`saveChanges` take `radioAffected`.

Notes for anyone extending this:

- `reloadConfig(X, /*radioAffected=*/false)` is **exactly equivalent** to `saveToDisk(X)` - the
  save is unconditional, outside the guard. So never pair them: that writes the file twice.
- **AdminModule keeps using `saveChanges`**, not `applyConfigChange` directly. It owns the
  edit-transaction deferral, without which a multi-field remote set writes flash per field.
- Reboots go through `requestReboot()` (`src/main.h`). It carries no UI: BaseUI already renders
  the notice at draw time from `rebootAtMsec`, while InkHUD's e-ink only draws when pushed and so
  raises `notifyApplyingChanges()` explicitly.
- `rebootSeconds` exists for one caller - InkHUD's wifi-recovery path uses a shorter delay than
  `DEFAULT_REBOOT_SECONDS`. A **negative** value cancels a pending reboot rather than bringing it
  forward (`rebootAtMsec == 0` means "none pending" everywhere it is read), matching
  `admin.proto`'s `reboot_seconds`.
- Every `CONFIG_APPLY_REBOOT` site in the InkHUD menu pairs with a `notifyApplyingChanges()`, and
  must keep doing so - see the two-jobs note on that method in `InkHUD.h`.
- `CONFIG_APPLY_RADIO` means the **modem**, not the segment. The channel menu's uplink/downlink
  and `position_precision` actions write `SEGMENT_CHANNELS` but touch no name, PSK or frequency
  slot, so they persist only; the frequency the radio derives comes from the name+PSK hash, and
  `RadioInterface` is the sole observer of `configChanged`. They carried the flag at first purely
  because the old `reloadConfig(SEGMENT_CHANNELS)` inferred it from the bitmask.
- UI-only state stays out of this: BaseUI `uiconfig` fields use `saveUIConfig()`, and InkHUD has
  its own non-protobuf `Persistence::Settings` store. Neither is in the `/prefs` protobuf tree.

---

## Verification

Native coverage lives in `test/test_admin_radio/test_main.cpp`:

- **Radio reload:** each non-LoRa `set_config` sub-message asserts `configChanged` does **not**
  fire (`ConfigChangedCounter`); `lora` and `set_channel` assert it still does; direct
  `reloadConfig` guards pin the fail-safe default and the explicit opt-out.
- **Reboot:** each of position/network/bluetooth asserts `rebootAtMsec` stays `0` on a no-op
  set and is armed on a real change; a position broadcast-interval change asserts **no**
  reboot, while `gps_mode` and `rx_gpio` changes assert a reboot.
- **Edit transactions:** the same two axes asserted across a `begin` / set / `commit` sequence,
  where the segment bitmask no longer backstops `radioAffected`. Node-DB, module-config and
  nested-position batches must commit without reconfiguring; a LoRa batch must still reconfigure
  (and not reboot); a `gps_mode` batch must reboot without reconfiguring. Also pinned: nothing is
  written until the commit, the flags don't leak into the next transaction, and a commit with no
  matching begin is inert.

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

### 1. Radio-reload / crash validation (regression guard for the favorite-node fix)

Confirms the non-LoRa config saves no longer run the live radio reconfigure - the main-task
`setStandby()` + SPI reprogram that collided with the radio's off-main worker thread over the
non-recursive `spiLock` and locked it up, tripping the watchdog reboot on the WisMesh Tag
favorite (#11146).

Perform each of: toggle Bluetooth `enabled`, change the WiFi PSK, rotate the security
keypair, favorite/unfavorite a node.

- **Pass:** no radio re-init in the serial log before any reboot (no modem-reconfigure /
  `setStandby` / re-`init` lines), and no watchdog reboot-loop. Saves that legitimately
  reboot (WiFi, keypair) do so _cleanly, after_ the save - the reboot is fine; a live
  reconfigure _before_ it is the failure.
- **Positive control:** a `lora` config change and a `set_channel` **should** show the
  reconfigure - proves the path still fires when it should.

**Then repeat the favorite/unfavorite case wrapped in `begin_edit_settings` /
`commit_edit_settings`** - that is the shape a phone app actually sends, and until the deferred
flags landed it was the one shape where the segment bitmask could not stop the reconfigure (see
"Edit transactions"). Pass criteria are the same, measured at the commit. Watch for a
reconfigure that appears only at the commit, not during the individual sets.

### 2. Position live-apply - expanding the Tier-2 live set (outstanding)

The only reason to touch a node for the _reboot_ work: decide whether the GPS-timing fields
left on the reboot path (`gps_mode`, `gps_enabled`, `gps_update_interval`, `gps_attempt_time` -
item 3 above) can actually apply live and be reclassified.

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

- To stop a config field from reloading the radio on an **AdminModule/client config save**: it
  already doesn't, unless it's `lora`. Do **not** widen `radioAffected` to non-LoRa config -
  only `RadioInterface::reconfigure()` (which reads `config.lora`) consumes it. The on-device
  menus are now gated the same way, via `applyConfigChange` (see "On-device menus" above).
- To move a field off the reboot path: confirm it is consumed live (read from `config` at use
  time, no cached/driver state), add it to the live set in the relevant `handleSetConfig`
  case, and add a native case asserting no reboot on its change. For anything driver- or
  hardware-backed, verify on real hardware first. When unsure, leave it rebooting.
