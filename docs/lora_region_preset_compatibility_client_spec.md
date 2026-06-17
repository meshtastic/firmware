# LoRa Region → Preset Compatibility — Client Implementation Spec

**Status:** Draft for 2.8 · **Audience:** Meshtastic client app developers (Android first,
Apple second, then web/python) · **Firmware side:** implemented in `firmware`
(`FromRadio.region_presets`, see below).

> This document lives in the firmware repo while the feature is developed. It is meant to
> graduate to `meshtastic/protobufs` (and/or the docs site) alongside the upstream protobuf
> PR that reserves `FromRadio` field **19**.

---

## 1. Why this exists

For 2.8 the LoRa regions and modem presets were reworked. **Not every modem preset is legal
in every region** — narrow EU SRD bands, the EU 868 "narrow" band, amateur/ham bands, and
the 2.4 GHz band each accept only a specific subset of presets. The firmware already
enforces this internally (it clamps or rejects illegal combinations), but until now a client
had no way to _know_ the rules, so a user could pick an illegal region+preset pair in the UI
and only discover the problem after the device silently corrected it.

This feature has the firmware **declare the legal region→preset combinations** to the client
during the `want_config` handshake, so the client UI can constrain the preset picker to the
valid set for the currently selected region (and warn about licensed-only bands). It is
purely advisory metadata — the firmware remains the source of truth and still
validates/clamps on its own.

---

## 2. Protocol additions

Three new messages in `meshtastic/mesh.proto`, plus one new `FromRadio` oneof variant.

### 2.1 `FromRadio.region_presets` (field 19)

```proto
message FromRadio {
  uint32 id = 1;
  oneof payload_variant {
    // ... fields 2..18 unchanged ...
    LoRaRegionPresetMap region_presets = 19;
  }
}
```

### 2.2 Messages

```proto
// A distinct set of legal modem presets shared by one or more LoRa regions.
message LoRaPresetGroup {
  repeated Config.LoRaConfig.ModemPreset presets = 1;       // legal presets for this group
  Config.LoRaConfig.ModemPreset         default_preset = 2; // always one of `presets`
  bool                                  licensed_only = 3;  // ham/amateur band → warn/gate
}

// Associates a single LoRa region with its preset group (by index).
message LoRaRegionPresets {
  Config.LoRaConfig.RegionCode region = 1;
  uint32                       group_index = 2; // index into LoRaRegionPresetMap.groups
}

// The full map, delivered grouped to fit one FromRadio packet.
message LoRaRegionPresetMap {
  repeated LoRaPresetGroup    groups = 1;        // each distinct preset list
  repeated LoRaRegionPresets  region_groups = 2; // every known region → a group index
}
```

### 2.3 Why grouped (and the size envelope clients should respect)

A `FromRadio` packet is capped at **512 bytes** (`MAX_TO_FROM_RADIO_SIZE`). Most regions
share one identical preset list (the "standard" 9-preset list), so the map is delivered
**grouped**: `groups` holds each _distinct_ preset list once, and `region_groups` maps every
known region to one of those groups by index. This keeps the encoded size additive
(`groups` + `region_groups`) rather than multiplicative, well under the cap.

nanopb (firmware) array bounds — clients do **not** need to enforce these, but they bound
what you can receive:

| field                               | max_count                            |
| ----------------------------------- | ------------------------------------ |
| `LoRaRegionPresetMap.groups`        | 8                                    |
| `LoRaRegionPresetMap.region_groups` | 38 (= number of `RegionCode` values) |
| `LoRaPresetGroup.presets`           | 11                                   |

---

## 3. When it is delivered

`region_presets` is sent **once** during the `want_config` handshake, as a single
`FromRadio` message, in this position:

```text
my_info → (deviceuiConfig) → node_info(self) → metadata → region_presets → channel… → config… → moduleConfig… → node_info(others)… → fileInfo… → config_complete_id → (live packets)
```

i.e. **immediately after `metadata` and before the first `channel`**.

- It is included for a normal full `want_config` and for the **config-only** nonce.
- It is **omitted** for the **nodes-only** nonce (that path skips metadata/config entirely).
- A client must **not** assume it always arrives (see §5).

---

## 4. Decoding into a usable lookup

Flatten the grouped wire form into `Map<RegionCode, RegionPresetInfo>`:

```text
struct RegionPresetInfo { Set<ModemPreset> presets; ModemPreset default; bool licensedOnly }

fun decode(map: LoRaRegionPresetMap): Map<RegionCode, RegionPresetInfo> {
  result = {}
  for (rg in map.region_groups) {
    if (rg.group_index >= map.groups.size) continue   // defensive: malformed/forward data
    g = map.groups[rg.group_index]
    result[rg.region] = RegionPresetInfo(
      presets = g.presets.toSet(),
      default = g.default_preset,
      licensedOnly = g.licensed_only)
  }
  return result
}
```

Persist this map alongside the rest of the downloaded config so the LoRa config screen can
read it synchronously.

---

## 5. Semantics & rules (the load-bearing part)

These rules are what keep the UX correct across firmware versions. Implement all of them.

1. **Absent region ⇒ no constraint.** If a `RegionCode` does not appear in `region_groups`,
   the client has _no_ compatibility info for it and **must not restrict** its preset
   choices (fall back to allowing the full `ModemPreset` list). This happens for a handful
   of `RegionCode` enum values that have no firmware band table entry (today: `EU_874`,
   `EU_917`, `ITU1_70CM`, `ITU2_70CM`, `ITU3_70CM`).

2. **Absent message ⇒ no constraint.** Firmware older than 2.8 never sends `region_presets`.
   New clients **must** tolerate the message being absent entirely and keep their existing
   (unconstrained) behavior. Do not block the config screen waiting for it.

3. **`default_preset`** is always a member of that group's `presets`. Use it to pre-select a
   preset when the user switches to a region whose valid set does not include the currently
   selected preset (instead of leaving an illegal selection or guessing).

4. **`licensed_only`** marks ham/amateur bands. Surface a warning or gate (the firmware also
   requires the operator's `is_licensed` flag for these regions; coordinate the two so the
   user isn't allowed to pick a licensed band without acknowledging licensing).

5. **EU region auto-swap caveat.** The firmware treats the EU sibling regions
   (`EU_868` / `EU_866` / `EU_N_868`) specially: if the user is in one of them and selects a
   preset that belongs to a sibling's list, the firmware **swaps the region** rather than
   rejecting the preset. Consequence for clients: **do not assume the region is immutable
   across a preset change** — after an admin config write, re-read the resulting
   `LoRaConfig` and reflect the (possibly changed) region back into the UI.

6. **Use it as a UI guard, not a validator of truth.** The firmware still validates/clamps
   on its own. The map exists to prevent the user from _selecting_ an illegal combo; it is
   not a security or correctness boundary.

---

## 6. UI/UX recommendations

- In the LoRa config screen, when a region is selected, **filter/enable the modem-preset
  picker to that region's `presets`** (when `use_preset`/`use_modem_preset` is on).
- If the current preset is not in the newly selected region's set, switch the selection to
  that region's `default_preset`.
- Show a **licensed badge / confirmation** for regions where `licensed_only == true`.
- If a region is absent from the map (rule §5.1) or the whole message is absent (§5.2),
  render the full preset list as before — never show an empty picker.

---

## 7. Forward / backward compatibility

- **Old clients, new firmware:** an unknown `FromRadio` oneof variant (field 19) is ignored
  by protobuf/nanopb decoders; the relative ordering of the known messages is unchanged, so
  existing apps are unaffected.
- **New clients, old firmware:** message simply never arrives → treat as "no constraints"
  (§5.2).
- **Enum growth:** new `RegionCode`/`ModemPreset` values may appear over time. Decoders
  should pass through unknown enum values rather than crashing; an unknown region in
  `region_groups` is harmless (the client just won't have a localized name for it).

---

## 8. Platform notes

> Verified against the `main` branch of each repo. Both have been refactored away from
> older layouts; re-pin file paths against a specific commit if you need them durable.

### 8.1 Android — `meshtastic/Meshtastic-Android` (Kotlin / Compose, KMP)

- **Protobufs are a published Maven artifact, _not_ a submodule.** Declared in
  `gradle/libs.versions.toml` (`org.meshtastic:protobufs`, currently `2.7.25`); generated
  package is **`org.meshtastic.proto`**. **A `region_presets`-aware build requires a new
  published `org.meshtastic:protobufs` release**, then bumping that one version string.
- **The protobufs are Wire-generated**, so the `FromRadio` oneof is **not** a
  `payloadVariantCase` enum — each arm is a **nullable field**. Handle the new variant in
  `FromRadioPacketHandlerImpl.handleFromRadio(...)`
  (`core/data/.../manager/FromRadioPacketHandlerImpl.kt`) by adding a
  `regionPresets != null -> …` arm to the existing `when { … }`, delegating to a handler
  (mirror `handleLocalMetadata` / `handleConfigComplete`).
- **State holder:** expose the decoded map from `RadioConfigRepository` /
  `RadioConfigRepositoryImpl` as a `Flow` (mirroring `localConfigFlow`/`channelSetFlow`),
  consumed by `feature/settings/.../radio/RadioConfigViewModel.kt`.
- **UI:** the region & preset dropdowns are `DropDownPreference`s in
  `feature/settings/.../radio/component/LoRaConfigItemList.kt` (public composable
  `LoRaConfigScreen`). Gate/filter the `ChannelOption` (preset) dropdown by the selected
  `RegionInfo`'s entry in the map.

### 8.2 Apple — `meshtastic/Meshtastic-Apple` (Swift / SwiftUI)

- **Protobufs are vendored** into a local Swift package `MeshtasticProtobufs`
  (`MeshtasticProtobufs/Sources/meshtastic/*.pb.swift`), generated from the `protobufs` git
  submodule via `scripts/gen_protos.sh`. **To get field 19:** advance the `protobufs`
  submodule, run `scripts/gen_protos.sh`, commit the regenerated `.pb.swift` + submodule
  pointer. (No published-artifact dependency — Apple can regenerate from any commit.)
- **Dispatch:** `AccessoryManager.processFromRadio(_:)`
  (`Meshtastic/Accessory/Accessory Manager/AccessoryManager.swift`) is a real
  `switch decodedInfo.payloadVariant { … }` — add a `.regionPresets` case, with the handler
  in `AccessoryManager+FromRadio.swift` (mirror `handleConfig` / `handleMetadata`).
- **Persistence:** config is **SwiftData** (`@Model` entities), upserted via
  `MeshPackets`/`UpdateSwiftData.swift`. Store the decoded map (e.g. on a settings/connection
  model) so the LoRa view can read it.
- **UI:** `Meshtastic/Views/Settings/Config/LoRaConfig.swift` (`struct LoRaConfig: View`)
  has the `Picker("Region", …)` (`RegionCodes.userSelectable`) and `Picker("Presets", …)`
  (`ModemPresets.userSelectable`, gated on `usePreset`). Filter the presets picker by the
  selected region's entry. Enums live in `Meshtastic/Enums/LoraConfigEnums.swift`.

### 8.3 Other clients

- **python (`meshtastic` / Meshtastic-python)** and **web** consume the published protobufs;
  they will see `region_presets` once their protobuf dependency includes field 19, and can
  ignore it until then (it decodes as an unknown field).

---

## 9. Reference payload (current firmware table)

For decoder unit tests. With the 2.8 region table, the firmware emits **6 groups**. Group
indices are assigned in region-table order (first region to use a profile creates its group),
so they are stable as listed here:

| group_index             | default_preset | licensed_only | presets                                                                                                        |
| ----------------------- | -------------- | ------------- | -------------------------------------------------------------------------------------------------------------- |
| 0 (standard)            | `LONG_FAST`    | false         | LONG_FAST, LONG_SLOW, MEDIUM_SLOW, MEDIUM_FAST, SHORT_SLOW, SHORT_FAST, LONG_MODERATE, SHORT_TURBO, LONG_TURBO |
| 1 (EU 868)              | `LONG_FAST`    | false         | LONG_FAST, LONG_SLOW, MEDIUM_SLOW, MEDIUM_FAST, SHORT_SLOW, SHORT_FAST, LONG_MODERATE                          |
| 2 (EU 866 SRD / "lite") | `LITE_FAST`    | false         | LITE_FAST, LITE_SLOW                                                                                           |
| 3 (EU 868 narrow)       | `NARROW_SLOW`  | false         | NARROW_FAST, NARROW_SLOW                                                                                       |
| 4 (ham 20 kHz)          | `TINY_FAST`    | **true**      | TINY_FAST, TINY_SLOW                                                                                           |
| 5 (ham 100 kHz)         | `NARROW_SLOW`  | **true**      | NARROW_FAST, NARROW_SLOW                                                                                       |

`region_groups` (region → group_index):

| group | regions                                                                                                                                                               |
| ----- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0     | US, EU_433, CN, JP, ANZ, ANZ_433, RU, KR, TW, IN, NZ_865, TH, UA_433, UA_868, MY_433, MY_919, SG_923, PH_433, PH_868, PH_915, KZ_433, KZ_863, NP_865, BR_902, LORA_24 |
| 1     | EU_868                                                                                                                                                                |
| 2     | EU_866                                                                                                                                                                |
| 3     | EU_N_868                                                                                                                                                              |
| 4     | ITU1_2M, ITU2_2M, ITU3_2M                                                                                                                                             |
| 5     | ITU2_125CM                                                                                                                                                            |

> Note groups **3** and **5** carry the same preset list (NARROW\_\*) but are distinct groups
> because they differ in `licensed_only`. Decoders must key on the group, not on the preset
> list, to preserve the licensing flag.
>
> Regions **absent** from the table (no constraint info; see §5.1): `EU_874`, `EU_917`,
> `ITU1_70CM`, `ITU2_70CM`, `ITU3_70CM`.

This table is generated from the firmware's region table at runtime; treat the firmware as
authoritative and these values as the expected snapshot for the 2.8 table.
