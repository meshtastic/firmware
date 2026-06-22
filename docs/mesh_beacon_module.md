# Mesh Beacon Module — Function, Settings, and Client Interface Spec

Status: draft, tracks firmware branch `feat/mesh-beacon`.
Audience: firmware reviewers (Part 1) and client-app developers — Android / Apple / Web / Python (Part 2).

The Mesh Beacon module lets a node periodically **advertise the existence of a mesh** to
nodes that are not yet on it — broadcasting a short human-readable message plus an optional
"join offer" (a channel, region, and modem preset). It is the mechanism behind invitations
like _"Join us on NarrowSlow"_: a node sitting on one preset/region can shout an invitation
that listeners on other presets/regions can hear and surface to their user.

The module is deliberately **advisory**. The firmware never auto-joins an advertised
channel or auto-switches preset/region in response to a received beacon — it delivers the
information to the client app and stops there. All "should I act on this?" decisions belong
to the client and, ultimately, the user.

---

## Part 1 — Function and settings choices

### 1.1 Two roles in one module

| Role            | Class                       | Active when                  | What it does                                                                                               |
| --------------- | --------------------------- | ---------------------------- | ---------------------------------------------------------------------------------------------------------- |
| **Broadcaster** | `MeshBeaconBroadcastModule` | `FLAG_BROADCAST_ENABLED` set | Periodically transmits `MESH_BEACON_APP` packets on the configured radio settings.                         |
| **Listener**    | `MeshBeaconListenerModule`  | `FLAG_LISTEN_ENABLED` set    | Receives `MESH_BEACON_APP` packets, delivers the text to the local inbox, caches the offer for the client. |

The boolean toggles live in a single `flags` bitfield (see [§1.8](#18-settings-reference-moduleconfigmeshbeaconconfig-tag-17)) — broadcasting and
listening can be enabled independently on the same node. The whole module compiles out under the
`MESHTASTIC_EXCLUDE_BEACON` build flag.

### 1.2 Wire message

Beacons travel on a dedicated port number:

```protobuf
MESH_BEACON_APP = 37          // meshtastic/portnums.proto
ENCODING: protobuf (meshtastic.MeshBeacon)
```

```protobuf
message MeshBeacon {
  string message                          = 1;  // human-readable text, max 100 bytes (buffer 101)
  ChannelSettings offer_channel           = 2;  // optional advertised channel (name + PSK + slot)
  Config.LoRaConfig.RegionCode offer_region = 3; // optional advertised region (UNSET = none)
  optional Config.LoRaConfig.ModemPreset offer_preset = 4; // optional advertised preset
}
```

`.options` size caps (enforced at generation and on send):
`message ≤ 100`, `offer_channel.name ≤ 12`, `offer_channel.psk ≤ 32`.

The three `offer_*` fields together describe _"there is a reachable mesh on this
region+preset, here is the channel to use."_ Any subset may be present; an empty message with
a populated offer (or vice-versa) is valid.

### 1.3 Transmission behaviour

Every outgoing beacon packet is stamped uniformly (`sendBeacon` → `stampPacket`):

- `to = NODENUM_BROADCAST`
- `from = local node` (see [§1.6](#16-broadcast_send_as_node-currently-disabled) for the disabled spoof path)
- **`hop_limit = 0`** — beacons are **zero-hop**. They are never rebroadcast by the mesh; only
  direct RF neighbours hear them. This is the primary spam-control mechanism.
- `priority = BACKGROUND`, `want_ack = false`.

Broadcasting is additionally gated at runtime by:

- airtime utilisation (`isTxAllowedAirUtil()`), and
- device role — **`CLIENT_HIDDEN` never broadcasts**.

#### Interval

`broadcast_interval_secs` controls cadence. The floor is **3600 s (1 hour)**
(`default_mesh_beacon_min_broadcast_interval_secs`); `0` means "use default". Values below the
floor are silently raised, both at config-set time (AdminModule) and at runtime.

#### Radio switching for TX

A beacon's whole point is often to reach a mesh on a _different_ preset/region/channel than the
broadcaster currently runs. Before transmitting a beacon tagged with target radio settings, the
module temporarily reconfigures the radio (`reconfigureForBeaconTX`), sends, then restores the
prior config. Per-packet target settings are held in an 8-entry **sidecar table** keyed by packet
ID — chosen so the `MeshPacket` proto carries no extra per-packet radio fields, and normal
(non-beacon) traffic is never touched.

Two safety guards run before any radio switch (`beaconTxConfigInvalid`):

1. **An unlicensed node never keys up on a licensed-only (ham) region.** (The reverse — a licensed
   node operating in a non-ham region — is allowed. The switch only touches preset/region/channel,
   never `owner.is_licensed`.)
2. **The preset must be valid for the target region** (`validateConfigLora`).

   If either fails, the radio is **not** switched and the radio driver **drops** the packet rather
   than letting it fall through onto the current config.

#### Channel encryption on an override channel

Encryption keys off the **primary** channel slot, and the radio-thread channel switch happens
_after_ encryption. So when a beacon goes out on an override channel (different name/PSK), the
module installs the beacon channel into the primary slot for the synchronous duration of
`send()`, then restores it (`sendBeaconPacket`). This guarantees the packet is encrypted with the
beacon channel's key and stamped with its hash — not the primary's. Meshtastic threading is
cooperative, so there is no preemption between swap and restore.

### 1.4 Where beacons are sent: single-target and multi-target

The broadcaster can send to one set of radio settings or to several. **Single- and multi-target
are equal options — neither is preferred and neither is legacy.** Pick whichever matches the
deployment.

- **Single-target:** the scalar `broadcast_on_preset` / `broadcast_on_region` /
  `broadcast_on_channel` fields describe one destination. Used when `broadcast_targets` is empty.
- **Multi-target:** `broadcast_targets` (repeated `BroadcastTarget`) describes several. When
  non-empty it takes over from the scalar `broadcast_on_*` fields, and the broadcaster sends **one
  beacon copy per entry**. Each `BroadcastTarget` is `{ optional preset, region, optional channel_index }`,
  where `channel_index` references a slot in the node's own channel table (the channel must already be
  configured locally — its key is needed to encrypt the beacon).

#### Same-settings vs. other-settings

Independent of single/multi, each destination can either reuse the node's **own current radio
settings** or specify **different** ones:

- **Same-settings ("message of the day"):** leave the preset / region / channel unset. They fall
  back to the running config, so the beacon goes out on the node's current mesh with **no radio
  switch** — a plain periodic broadcast to whoever is already on this preset/region.
- **Other-settings (cross-mesh invite):** set a preset / region / channel that differs from the
  running config. The radio is temporarily switched for that copy's TX, then restored (see
  [§1.3](#radio-switching-for-tx)).

Both modes support both styles: a single-target beacon with no `broadcast_on_*` overrides is a
message-of-the-day on the current mesh; a multi-target list can mix one entry on the current
settings with others on different presets/regions.

### 1.5 Legacy split (`FLAG_LEGACY_SPLIT`)

A combined `MESH_BEACON_APP` packet carries both the text and the offer, but firmware that
predates this module only decodes `TEXT_MESSAGE_APP` and would never show the text. When
`FLAG_LEGACY_SPLIT` is set **and both text and offer content are present**, the broadcaster
emits **two** packets on the same beacon radio settings instead of one:

- **Packet A** — `MESH_BEACON_APP` carrying the **offer only** (no text).
- **Packet B** — `TEXT_MESSAGE_APP` carrying the **text only**.

This is an independent two-packet decision, not an either/or: offer-only and text-only payloads
still go out as a single packet in their respective cases; only the both-present case splits.

### 1.6 `broadcast_send_as_node` (currently disabled)

The schema reserves `broadcast_send_as_node` (field 3) to send beacons _as_ another node ID. **The
firmware application of this field is currently commented out pending review**, so beacons always
go out as the local node today. The access-control rule is, however, already enforced in
AdminModule and should be treated as canonical:

> A remote admin may only set `broadcast_send_as_node` to **their own** node ID
> (`mp.from`). Any other value is rejected and reset to the stored value.

Design note for when it is re-enabled: it is a _node-ID_ spoof only — it rewrites `from` but forges
no signature. Once `from` is not us, the packet is no longer `isFromUs()`, so the router skips
XEdDSA signing and receivers get an unsigned packet attributed to another node.

### 1.7 Reception behaviour (listener)

`wantPacket` accepts a packet only when `has_mesh_beacon` and `FLAG_LISTEN_ENABLED` is set, and
`portnum == MESH_BEACON_APP`. On a valid beacon (`handleReceivedProtobuf`):

1. **Text → local inbox.** If the beacon has text, the listener delivers a `TEXT_MESSAGE_APP`
   copy to the **locally connected phone only** via `sendToPhone()` — preserving the original
   `from` (the real beaconer). This is a _local inbox delivery, not a mesh retransmit_: it is never
   re-injected into the mesh, so the text is neither amplified nor re-attributed to the listener.
2. **Offer → cache.** Any offer (`offer_channel` / `offer_region` / `offer_preset`) is stored in
   the static `lastReceivedOffer` (sender, channel, region, preset, `received_at`). `received_at`
   is `0` if the node has no RTC fix yet — **consumers must not treat `0` as a valid timestamp.**
3. **Never auto-applied.** The firmware does not switch channel/preset/region from a received
   offer. Acting on it is the client app's job.
4. The handler returns `CONTINUE` (not `STOP`), so the original `MESH_BEACON_APP` packet **also
   flows to the phone** through the normal FromRadio path (see Part 2).

### 1.8 Settings reference (`ModuleConfig.MeshBeaconConfig`, tag 17)

| #   | Field                     | Type                     | Meaning / constraints                                                                                  |
| --- | ------------------------- | ------------------------ | ------------------------------------------------------------------------------------------------------ |
| 1   | `flags`                   | uint32 (bitfield)        | Bitwise-OR of `Flags` values (listen / broadcast / legacy-split toggles). See enum below.              |
| 3   | `broadcast_send_as_node`  | uint32                   | Send-as node ID. **Application disabled in firmware.** Remote admin may only set to own node ID.       |
| 4   | `broadcast_message`       | string                   | Text in each broadcast. **Hard-capped at 100 bytes.**                                                  |
| 5   | `broadcast_offer_channel` | ChannelSettings          | Channel advertised in `offer_channel`.                                                                 |
| 6   | `broadcast_offer_region`  | RegionCode               | Region advertised in `offer_region`. Must be a known region or it is cleared.                          |
| 7   | `broadcast_offer_preset`  | optional ModemPreset     | Preset advertised in `offer_preset`. Validated against offer region (else cleared).                    |
| 8   | `broadcast_on_channel`    | ChannelSettings          | Channel to transmit on (single-target). Empty name → preset display name.                              |
| 9   | `broadcast_on_region`     | RegionCode               | Region to transmit on (single-target).                                                                 |
| 10  | `broadcast_on_preset`     | optional ModemPreset     | Preset to transmit on (single-target). Validated against on-region (else this + `on_channel` cleared). |
| 11  | `broadcast_interval_secs` | uint32                   | Cadence. **Min 3600**, default 3600; `0` = default.                                                    |
| 13  | `broadcast_targets`       | repeated BroadcastTarget | Multi-target list; when non-empty overrides the single-target `broadcast_on_*` fields.                 |

> The three boolean toggles were folded into the `flags` bitfield; field tags 2 and 12 are now
> unused (the branch is unreleased, so the old tags are left as gaps rather than reserved).

**`Flags` enum** (nested in `MeshBeaconConfig`; OR the values into `flags`):

| Bit value | Name                     | Meaning                                                                        |
| --------- | ------------------------ | ------------------------------------------------------------------------------ |
| 0         | `FLAG_NONE`              | No options enabled.                                                            |
| 1         | `FLAG_LISTEN_ENABLED`    | Receive beacons; deliver text to inbox, cache offer.                           |
| 2         | `FLAG_BROADCAST_ENABLED` | Periodically broadcast beacons from this node.                                 |
| 4         | `FLAG_LEGACY_SPLIT`      | Split text+offer into separate `TEXT_MESSAGE_APP` + `MESH_BEACON_APP` packets. |

`BroadcastTarget`: `1 preset` (optional, falls back to running config), `2 region` (`UNSET` = running config), `4 channel_index` (optional `uint32`, index into the node's channel table; if unset, the default channel for the preset is used). Tag `3` is an unused gap — it previously held an embedded `ChannelSettings`, dropped to keep `ModuleConfig` within the BLE `FromRadio` size budget.

---

## Part 2 — Client interface specification

This section is what a client app needs to integrate with the beacon module. Everything goes
through the **standard admin / ToRadio / FromRadio protocol** — there is no bespoke transport.

### 2.1 Capability detection

The module is build-flag optional. Treat it as present when the node's `LocalModuleConfig`
contains a `mesh_beacon` sub-message (`LocalModuleConfig.mesh_beacon`, tag 18). If absent, the
firmware was built with `MESHTASTIC_EXCLUDE_BEACON` — hide the beacon UI.

### 2.2 Reading and writing configuration

Standard module-config flow — no new admin messages:

- **Read:** `AdminMessage.get_module_config_request = ModuleConfig.MeshBeaconConfig` (variant 17).
  Reply is `get_module_config_response` with the `mesh_beacon` payload.
- **Write:** `AdminMessage.set_module_config { mesh_beacon = … }`.

The on/off toggles (listen, broadcast, legacy-split) are bits in the `flags` field, not separate
booleans — read/write them with the `MeshBeaconConfig.Flags` values
(`FLAG_LISTEN_ENABLED = 1`, `FLAG_BROADCAST_ENABLED = 2`, `FLAG_LEGACY_SPLIT = 4`). To toggle one
bit, read the current `flags`, set/clear the bit, and write the whole config back.

The firmware **sanitises on write** — your value may be silently adjusted. Mirror these rules
client-side so the UI doesn't disagree with the device:

| Rule                                                                        | Firmware behaviour                                                        |
| --------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| `broadcast_message` length                                                  | Truncated to 100 bytes.                                                   |
| `broadcast_interval_secs`                                                   | If non-zero and `< 3600`, raised to 3600.                                 |
| `broadcast_on_preset` invalid for `broadcast_on_region` (or current region) | Cleared, **and `broadcast_on_channel` cleared too.**                      |
| `broadcast_offer_preset` invalid for offer/current region                   | Cleared.                                                                  |
| `broadcast_offer_region` not a known region                                 | Cleared to `UNSET`.                                                       |
| `broadcast_targets[i].region` not a known region                            | That entry's region cleared to `UNSET` (TX falls back to running config). |
| `broadcast_targets[i].preset` invalid for that entry's region               | That entry's `preset` and `channel` cleared.                              |
| `broadcast_send_as_node` ≠ sender's node ID (remote admin)                  | Rejected, reset to stored value.                                          |

Setting beacon config does **not** trigger a reboot (`shouldReboot = false`); changes take effect
on the next broadcast cycle. After a successful write, **re-read** the config to display the
effective (sanitised) values.

### 2.3 Receiving beacons

A received beacon reaches the client as a normal `FromRadio.packet` (`MeshPacket`) — the listener
returns `CONTINUE`, so the packet is **not** consumed on-device. The client must:

1. Subscribe to the FromRadio packet stream as usual.
2. For packets with `decoded.portnum == MESH_BEACON_APP (37)`, decode `decoded.payload` as a
   `meshtastic.MeshBeacon`.
3. Read `message`, `offer_channel`, `offer_region`, `offer_preset` (presence-checked).
4. `packet.from` is the **originating beaconer** (the firmware preserves it).

> **Requires `FLAG_LISTEN_ENABLED` set in `flags`.** With listening disabled the firmware does not
> surface beacons. (A node still physically receives RF, but `wantPacket` rejects it before the
> phone sees it.)

#### Text duplication — important

For beacons that carry text, the client may observe the text **twice**:

- once as the original `MESH_BEACON_APP` packet (`message` field), and
- once as a `TEXT_MESSAGE_APP` packet that firmware injects into the local inbox so it appears in
  the normal message list (same `from`, same text).

Plus, if the broadcaster used legacy split (`FLAG_LEGACY_SPLIT`), the text and offer arrive as two distinct
RF packets to begin with. **Deduplicate** when presenting messages — e.g. key on
`(from, text, ~timestamp)` — so a single beacon does not show as multiple chat entries. A common
strategy: treat the `TEXT_MESSAGE_APP` copy as the chat entry, and treat `MESH_BEACON_APP` purely
as the source of the _offer_.

### 2.4 Acting on an offer (the core client responsibility)

When a `MESH_BEACON_APP` carries offer content, present it to the user as an **invitation** —
e.g. _"Node ⟨from⟩ invites you to join '⟨offer_channel.name⟩' on ⟨preset⟩/⟨region⟩."_ Then, only on
explicit user confirmation, apply it by writing normal config:

- `offer_channel` → add/replace a `Channel` (`set_channel`), typically as a secondary channel.
- `offer_region` / `offer_preset` → `set_config { lora = … }` (`use_preset = true`, set
  `modem_preset` and `region`). **Note this changes the node's own radio and will drop it off its
  current mesh** — make that consequence explicit in the UI.

**The firmware will never do any of this for the user. No silent auto-apply.** The on-device
`lastReceivedOffer` cache is a firmware-internal convenience and is **not** currently exposed via
an admin message — clients should source offers from the live `MESH_BEACON_APP` packet stream
(§2.3), not expect a "get last offer" RPC.

#### Offer trust model — read before applying

- **The advertised PSK is not a secret.** `offer_channel.psk` is a public join token sent in the
  clear inside a broadcast; it is a convenience, not a security boundary. An operator who wants a
  genuinely private channel must distribute the PSK out-of-band and leave `offer_channel` unset.
  Surface offered channels as **public/open** to the user.
- **Validate before applying.** Reject or warn if `offer_preset` is not valid for `offer_region`,
  and **never** apply a licensed-only (ham) region for a user who is not a licensed operator —
  mirror the firmware's own guard.
- Beacons are **unsigned** when sent as another node (the disabled send-as path), and even normal
  beacons assert nothing about the sender's authority. Treat `from` as informational.

### 2.5 Configuring this node as a broadcaster

To make a node advertise a mesh, write `MeshBeaconConfig` with `FLAG_BROADCAST_ENABLED` set in
`flags` and at least one of: a non-empty `broadcast_message`, or offer content
(`broadcast_offer_*`). With neither, the broadcaster has nothing to send and stays silent.

Typical multi-region invite beacon:

```text
flags                   = FLAG_BROADCAST_ENABLED | FLAG_LEGACY_SPLIT   // broadcast on; split so legacy nodes still see the text
broadcast_message       = "Join us on NarrowSlow!"
broadcast_offer_preset  = NARROW_SLOW
broadcast_offer_region  = EU_N_868
broadcast_offer_channel = { name: "MyChannel", psk: <32-byte key> }
broadcast_interval_secs = 3600
// channel_index points at slots in THIS node's channel table — configure those channels first.
broadcast_targets = [
  { preset: LONG_FAST,   region: EU_868,   channel_index: 0 },
  { preset: NARROW_SLOW, region: EU_N_868, channel_index: 1 },
]
```

The same fields can be baked in at build time via `userPrefs.jsonc`
(`USERPREFS_MESH_BEACON_*`) — see that file for the full list, including
`USERPREFS_MESH_BEACON_TARGET_<n>_*` for multi-target entries.

#### Single-target vs. multi-target — equal options, different channel representation

Single-target and multi-target are **equal, first-class options**. Neither is preferred,
deprecated, or a "legacy" fallback — pick whichever matches the deployment (a single-target
beacon with no overrides is a plain message-of-the-day; a multi-target list reaches several
preset/region/channel combinations). The broadcaster uses `broadcast_targets` when it is
non-empty and the scalar `broadcast_on_*` fields when it is empty.

The one **subtle implementation difference** is how each names its TX channel:

| Path          | TX channel is specified by                              | Channel name/PSK live…                    |
| ------------- | ------------------------------------------------------- | ----------------------------------------- |
| Single-target | `broadcast_on_channel` — an embedded `ChannelSettings`  | …inline in the beacon config              |
| Multi-target  | `broadcast_targets[i].channel_index` — a `uint32` index | …in the node's channel table (referenced) |

This asymmetry is deliberate: embedding a full `ChannelSettings` in every one of the (up to
four) targets would push `ModuleConfig` past the BLE `FromRadio` size limit, so a target
references an already-configured channel-table slot instead. `broadcast_offer_channel` (the
advertised join token) is **always** inline regardless of path — it is the advertisement payload
and must carry the actual name/PSK.

#### Configuring a multi-target broadcaster (two-step)

Because a target's channel is a reference, configuring a multi-target broadcaster takes **two
admin writes**, in order:

1. **Create/define each channel in the node's channel table** with the normal channel admin flow
   (the same `set_channel` your app already uses for adding channels):

   ```text
   AdminMessage.set_channel { index: 1, role: SECONDARY,
                              settings: { name: "NarrowSlow", psk: <key>, channel_num: 0 } }
   ```

2. **Write the beacon config**, pointing each target at the slot index from step 1:

   ```text
   AdminMessage.set_module_config { mesh_beacon: {
       flags = FLAG_BROADCAST_ENABLED
       broadcast_targets = [ { preset: NARROW_SLOW, region: EU_N_868, channel_index: 1 } ]
   } }
   ```

Notes:

- A target may **only** reference a channel that already exists locally — the node needs that
  channel's key to encrypt the beacon. A `channel_index` that is out of range, or points at a
  blank/unconfigured slot, falls back to the **default channel for the target preset** (the
  preset's display name, e.g. `LongFast`), not an error.
- `channel_index` must be `< MAX_NUM_CHANNELS`; the firmware clears it on write otherwise (see
  §2.2 sanitise rules).
- The single-target path needs no separate `set_channel` step — its `broadcast_on_channel` is
  written inline in the same beacon-config message.

### 2.6 Quick reference

| Concern                | Value                                            |
| ---------------------- | ------------------------------------------------ |
| Port number            | `MESH_BEACON_APP = 37`                           |
| Wire message           | `meshtastic.MeshBeacon`                          |
| Config message         | `ModuleConfig.MeshBeaconConfig` (variant tag 17) |
| On/off toggles         | `flags` bitfield (`MeshBeaconConfig.Flags`)      |
| Local config presence  | `LocalModuleConfig.mesh_beacon` (tag 18)         |
| Min broadcast interval | 3600 s (1 h)                                     |
| Message max length     | 100 bytes                                        |
| Hop behaviour          | Zero-hop (`hop_limit = 0`), never rebroadcast    |
| Auto-apply offers?     | **Never** — client + user decide                 |
| Offer PSK              | Public join token, not a secret                  |
| Disabled today         | `broadcast_send_as_node` application             |
