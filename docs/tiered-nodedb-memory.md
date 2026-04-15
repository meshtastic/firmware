# Tiered NodeDB Phase Memory

Append new sections to this file as each phase lands. The goal is to preserve the reasoning, user-visible effects, and any constraints the next phase must keep in mind.

## Phase 0: Decouple `MAX_NUM_NODES` From Unrelated RAM Users

Date: 2026-04-13
Status: Implemented

### Goal

Make future NodeDB cap increases safe by removing places where unrelated subsystems scaled themselves from `MAX_NUM_NODES`.

### What Changed

- `src/graphics/Screen.cpp`
  - Replaced the `normalFrames` allocation that scaled with `MAX_NUM_NODES` with a fixed screen frame cap.
  - Added a guard for module-frame insertion so the screen frame array stays bounded.
  - Capped favorite-node frames to the remaining screen frame budget and kept `UIRenderer::favoritedNodes` aligned with the actual number of favorite frames exposed to the carousel.
- `src/mesh/PacketHistory.cpp`
  - Replaced the `MAX_NUM_NODES * 2` sizing rule with an independent fixed default capacity of `256` packet-history records.
  - This keeps packet dedupe history tied to traffic behavior instead of NodeDB capacity.
- `src/graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.cpp`
  - Removed the `MAX_NUM_NODES` fullness heuristic from the header text.
  - The applet now always shows the heard-node count instead of hiding it once the database reaches a threshold.

### Important Constraints For Later Phases

- Screen favorite frames are now a bounded UI resource. Future favorite-frame changes must keep the frame count and `UIRenderer::favoritedNodes` in sync.
- `PacketHistory` is now intentionally independent from NodeDB sizing. Do not reconnect packet-history capacity to `MAX_NUM_NODES` in later phases.
- Phase 0 does not change NodeDB storage, persistence, sort order, or pointer semantics.

### Verification Target

- `native`
- `tbeam-s3-core`
- `rak4631`

### Follow-Up Notes

- If a later phase changes how favorites are ordered or displayed, revisit the favorite-frame truncation path in `Screen::setFrames()`.
- If traffic measurements show `256` packet-history entries is wrong, adjust it as a packet-routing tuning decision, not as a NodeDB-cap decision.

## Phase 1: Add Explicit Favorite / Ignore / Mute Mutators

Date: 2026-04-13
Status: Implemented

### Goal

Stop external callers from writing durable node flags directly through `meshtastic_NodeInfoLite *`.

### What Changed

- `src/mesh/NodeDB.h`
  - Added explicit durable mutators: `setFavorite(NodeNum, bool)`, `setIgnored(NodeNum, bool)`, and `setMuted(NodeNum, bool)`.
  - Kept a temporary `set_favorite(bool, uint32_t)` wrapper so non-phase-1 callers can continue building while later phases migrate the remaining call sites.
- `src/mesh/NodeDB.cpp`
  - Moved favorite writes onto `NodeDB::setFavorite()`, which preserves the existing sort-and-save behavior.
  - Added `NodeDB::setIgnored()` so ignore writes now persist through one NodeDB-owned path instead of each caller mutating fields and saving independently.
  - Added `NodeDB::setMuted()` so mute writes also persist through NodeDB instead of direct bitfield edits in UI/admin code.
  - Centralized the admin-style ignore scrub in NodeDB: setting `is_ignored=true` now clears cached device metrics, cached position data, and the stored public key before saving.
- `src/modules/AdminModule.cpp`
  - Replaced direct writes for auto-favorite, set/remove favorite, set/remove ignored, and toggle muted with the new NodeDB mutators.
  - Removed the duplicated `saveChanges(SEGMENT_NODEDATABASE, false)` calls from those flag-only paths because the NodeDB mutators now persist the change directly.
- `src/graphics/draw/MenuHandler.cpp`
  - Replaced direct favorite / ignore / mute writes with the new NodeDB mutators.
  - Kept the existing menu-side UI refresh behavior (`notifyObservers(true)` and `screen->setFrames(...)`) while dropping the duplicated raw save calls.

### Important Constraints For Later Phases

- Durable writes for favorite / ignore / mute should continue to enter through NodeDB mutators; later storage backends should preserve that contract rather than reintroducing caller-side field edits.
- The legacy `set_favorite()` wrapper still exists only as a compatibility bridge. Future phases should migrate remaining callers and remove it once no external code depends on the old API.
- Phase 1 does not change pointer-returning lookup APIs, storage order, sorting semantics, or persistence format.
- `setIgnored(true)` now owns the ignore scrub for admin/menu flows. If a later phase changes what “ignored” means, update the NodeDB mutator instead of reintroducing per-caller cleanup logic.

### Verification

- `pio run -e tbeam-s3-core`
- `pio run -e rak4631`
- `pio run -e native` currently fails locally in the existing Portduino/LovyanGFX toolchain path before linking NodeDB changes (`malloc.h` / missing C stdlib declarations in LovyanGFX sources).

### Follow-Up Notes

- `CannedMessageModule` still uses the old `set_favorite()` wrapper and should move to `setFavorite()` in a later API-hardening phase.
- Key-verification and local-node time writes still mutate durable state through raw node pointers; those are the next mutator candidates called out in phase 2.

## Phase 2: Add Key-Verification And Local-Node Mutators

Date: 2026-04-14
Status: Implemented

### Goal

Remove the remaining direct durable writes that would break once full node records stop living in one mutable RAM vector.

### What Changed

- `src/mesh/NodeDB.h`
  - Added `setKeyVerified(NodeNum, bool)` for durable manual-key verification writes.
  - Added `touchLocalNodeTime()` so local `last_heard` / `position.time` refreshes happen inside NodeDB instead of open-coded pointer mutation in callers.
- `src/mesh/NodeDB.cpp`
  - Implemented `setKeyVerified()` as the NodeDB-owned path for toggling `NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK`, with persistence handled in the mutator.
  - Implemented `touchLocalNodeTime()` to ensure the local node has a position struct, then refresh `last_heard` and `position.time` from the current valid network time.
- `src/modules/KeyVerificationModule.cpp`
  - Replaced direct `bitfield` writes in the admin verify path and the incoming-verification accept path with `nodeDB->setKeyVerified(...)`.
  - Captured the target `NodeNum` in the screen callback so delayed acceptance keeps referring to the intended node instead of whatever `currentRemoteNode` becomes later.
- `src/graphics/draw/MenuHandler.cpp`
  - Replaced the key-verification accept banner's direct `bitfield` write with `nodeDB->setKeyVerified(...)`.
  - Captured the selected `NodeNum` before building the callback for the same reason as the module-side banner path.
- `src/mesh/MeshService.cpp`
  - Replaced the local `last_heard` / `position.time` pointer writes in `refreshLocalMeshNode()` with `nodeDB->touchLocalNodeTime()`.
  - Kept battery refresh behavior in `MeshService`, since that remains outside NodeDB ownership.

### Important Constraints For Later Phases

- Manual key-verification writes should continue to enter through `NodeDB::setKeyVerified()` so persistence and future backend-specific record updates stay centralized.
- `touchLocalNodeTime()` is intentionally transient. It refreshes the in-memory local record for current time-sensitive behavior, but it does not save the NodeDB to disk.
- Key-verification accept flows now capture `NodeNum` by value before asynchronous UI callbacks fire. Keep that pattern if later phases replace pointer reads with scratch-buffer reads or `NodeNum` handles.
- Phase 2 does not change pointer-returning lookup APIs generally, display/storage order, or persistence format beyond ensuring manual key verification now persists through a mutator.

### Verification

- `pio run -e tbeam-s3-core`
- `pio run -e rak4631`
- `pio run -e native` still fails in the existing Portduino/LovyanGFX toolchain path before NodeDB changes are linked. The current local failure is still the same class of issue seen in phase 1: LovyanGFX native sources hit missing C runtime declarations (`size_t`, `memcpy`, `memmove`, `strlen`) and `malloc.h` include failures on macOS/Portduino.

### Follow-Up Notes

- `MeshService::refreshLocalMeshNode()` still returns a raw pointer for read-side compatibility; phase 3+ can tighten that once long-lived pointer caches start moving to `NodeNum`.
- Other direct raw-pointer reads remain throughout the codebase, but phase 2 removes the last key-verification and local-time write sites outside NodeDB.

## Phase 3: Replace Long-Lived Pointer Caches With `NodeNum`

Date: 2026-04-14
Status: Implemented

### Goal

Stop UI and module code from retaining `meshtastic_NodeInfoLite *` across frame rebuilds or delayed selection flows.

### What Changed

- `src/graphics/draw/UIRenderer.h`
  - Changed `UIRenderer::favoritedNodes` from `std::vector<meshtastic_NodeInfoLite *>` to `std::vector<NodeNum>`.
- `src/graphics/draw/UIRenderer.cpp`
  - Rebuilt the favorite-node cache as `NodeNum` values instead of raw node pointers.
  - Updated `drawNodeInfo()` to resolve the current favorite node via `nodeDB->getMeshNode()` at draw time before reading user, signal, or hops fields.
  - Kept `currentFavoriteNodeNum` as the durable hand-off for favorite-node menu actions, so menu paths no longer depend on frame-stable node pointers.
- `src/modules/CannedMessageModule.h`
  - Changed destination-selection cache entries from raw `NodeInfoLite *` to `NodeNum`.
- `src/modules/CannedMessageModule.cpp`
  - Rebuilt the filtered destination list as `NodeNum` values.
  - Updated destination selection to refresh the node's channel from `NodeDB` at selection time instead of reusing a cached pointer.
  - Updated destination-list drawing to re-read the current node record from `NodeDB` for names and favorite state, with `getNodeName()` fallback if the node disappeared after the list was built.
- `src/graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.cpp`
  - Replaced the stale prefill path's temporary pointer list with `NodeNum + last_heard` entries collected via `getMeshNodeByIndex()`.
  - Re-resolved each node by `NodeNum` before reading hops or position data to populate Heard cards.

### Important Constraints For Later Phases

- Favorite-node carousel state is now identity-based. If later phases replace `getMeshNode()` reads with scratch-buffer readers, keep `currentFavoriteNodeNum` and `favoritedNodes` as `NodeNum`-based state rather than reintroducing retained full-record pointers.
- Canned-message destination selection is now also identity-based. Later read-side migrations can swap the fresh `getMeshNode()` calls for `readNode()`-style helpers without changing the UI state shape.
- This phase does not change NodeDB lookup APIs, display/storage order, or persistence format. It only tightens caller-side cache lifetime assumptions ahead of the later `displayOrder[]` and storage-backend phases.

### Verification

- `pio run -e tbeam-s3-core`
- `pio run -e rak4631`
- `pio run -e native` still fails before NodeDB code is exercised in the existing Portduino/LovyanGFX macOS toolchain path. The current local failure remains the same class seen in phases 1 and 2: missing C runtime declarations in LovyanGFX C sources (`size_t`, `memcpy`, `memmove`, `strlen`) plus missing `malloc.h`.

### Follow-Up Notes

- `UIRenderer::drawNodeInfo()` and destination-list rendering still perform fresh pointer reads through `getMeshNode()` on use. That is intentional for phase 3, but phase 4+ should treat these sites as candidates for scratch-buffer read helpers once `displayOrder[]` and backend-backed full records land.
- `InkHUD::NodeListApplet` still re-reads live nodes through `getMeshNode(nodeNum)` while rendering cards. Phase 3 keeps that behavior because it is an on-use read rather than a retained pointer cache.

## Phase 4: Add `displayOrder[]` Without Changing Storage

Date: 2026-04-14
Status: Implemented

### Goal

Introduce a separate presentation-order view while keeping `meshNodes` as the live backing store and preserving current user-visible order.

### What Changed

- `src/mesh/NodeDB.h`
  - Moved `getMeshNodeByIndex()` out of line so index resolution can flow through an internal presentation-order map.
  - Added a private `displayOrder` vector plus a `resetDisplayOrder()` helper for maintaining the current identity mapping.
- `src/mesh/NodeDB.cpp`
  - Added `resetDisplayOrder()` to rebuild `displayOrder[i] = i` for the live node range.
  - Rebuilt `displayOrder` after NodeDB lifecycle paths that change the live set shape: default install, load, cleanup, reset, explicit removal, and node creation/eviction.
  - Changed `getMeshNodeByIndex()` to read through `displayOrder` instead of indexing `meshNodes` directly.
  - Changed `readNextMeshNode()` to iterate through `displayOrder`, so phone/export-style sequential enumeration now follows the presentation view instead of assuming storage order.
  - Kept `sortMeshDB()` physically sorting `meshNodes` in this phase, so behavior remains unchanged while the presentation path is now routed through a separable layer.

### Important Constraints For Later Phases

- In phase 4, `displayOrder` is still an identity view over the current storage layout. Any phase-4-era storage compaction or append path must continue to rebuild that identity mapping so callers never see stale storage indices.
- Phase 5 should change `sortMeshDB()` to reorder `displayOrder` instead of swapping full `meshtastic_NodeInfoLite` records. Once that lands, storage-mutating paths like reset/remove/evict/cleanup will need to preserve or deliberately rebuild presentation order with that new contract in mind.
- `getMeshNode(NodeNum)` still linearly scans `meshNodes`. Phase 4 does not add `NodeMeta[]`, slot identity, or hash lookup yet; it only decouples presentation reads from direct raw-storage indexing.

### Verification

- `pio run -e tbeam-s3-core`
- `pio run -e rak4631`
- `pio run -e native` still fails before NodeDB code is exercised in the existing Portduino/LovyanGFX macOS toolchain path. The local failure remains the same class seen in phases 1-3: LovyanGFX C/C++ sources hit missing C runtime declarations (`size_t`, `memcpy`, `memmove`, `strlen`) and missing `malloc.h`.

### Follow-Up Notes

- Phase 4 deliberately keeps storage order and presentation order aligned by identity. That keeps the diff small, but it also means phase 5 is the first point where `displayOrder` becomes semantically meaningful instead of just structurally present.
- Because `readNextMeshNode()` now depends on `displayOrder`, future storage backends can switch sequential export/read callers over to new record stores without rewriting those enumeration sites again.

## Phase 5: Stop Physically Sorting Storage

Date: 2026-04-14
Status: Implemented

### Goal

Make sorting reorder only the presentation view so full `meshtastic_NodeInfoLite` records stay in storage order.

### What Changed

- `src/mesh/NodeDB.cpp`
  - Changed `sortMeshDB()` to bubble-sort `displayOrder` entries instead of swapping full node records in `meshNodes`.
  - Kept the existing presentation semantics: local node first, favorites before non-favorites, then `last_heard` descending.
  - Added display-order maintenance helpers so storage compaction paths update `displayOrder` by removing/remapping shifted storage indices instead of rebuilding it as identity.
  - Updated `cleanupMeshDB()` and `removeNodeByNum()` to preserve the existing presentation order of surviving nodes across compaction.
  - Updated `getOrCreateMeshNode()` so append and eviction paths keep the current presentation order intact, then place a newly-created storage slot at the end until a later sort pass moves it.
  - Tightened `resetNodes(keepFavorites)` to compact favorites into the live range before rebuilding and re-sorting the presentation view.

### Important Constraints For Later Phases

- Storage order is no longer the user-visible order. Any caller that needs presentation order must continue to use `getMeshNodeByIndex()` or `readNextMeshNode()` rather than iterating raw `meshNodes` storage.
- After phase 5, `resetDisplayOrder()` is only safe when intentionally rebuilding the presentation view from scratch and then immediately re-sorting or otherwise re-establishing its contract. Using it casually after compaction will destroy the user-visible order.
- `sortMeshDB()` now mutates only `displayOrder`. Future slot-store phases should preserve that rule rather than reintroducing full-record swaps.
- Because storage order now mostly reflects append/compaction history, any later logic that wants stable identity should reason in terms of `NodeNum`, display index, or eventual slot/meta handles, not raw storage positions.

### Verification

- `pio run -e tbeam-s3-core`
- `pio run -e rak4631`
- `pio run -e native` still fails before NodeDB code is exercised in the existing Portduino/LovyanGFX macOS toolchain path, matching the earlier phases' local native blocker (`malloc.h` and missing C runtime declarations in LovyanGFX sources).

### Follow-Up Notes

- Phase 5 leaves `getMeshNode(NodeNum)` as a linear scan over storage order. That is fine for this phase, but phase 6+ should treat it as an implementation detail rather than evidence that storage order is meaningful.
- The next phase can add `NodeMeta[]` without needing to untangle sort-induced record movement first, because presentation ordering is now isolated in `displayOrder`.

## Phase 6: Add `NodeMeta[]` Shadow Cache

Date: 2026-04-14
Status: Implemented

### Goal

Introduce compact DRAM-side metadata while still treating the vector-backed `meshNodes` store as the source of truth.

### What Changed

- `src/mesh/NodeDB.h`
  - Added a private `NodeMeta` struct and `nodeMeta` vector aligned to the live storage slots.
  - Added metadata helpers for rebuild, per-slot refresh, storage-index recovery, linear metadata lookup, and metadata-side sort comparison.
  - Added metadata flags for favorite / ignored / key-verified / muted / MQTT / public-key / user / licensed / position / device-metrics state.
- `src/mesh/NodeDB.cpp`
  - Added `rebuildNodeMeta()` and `refreshNodeMeta()` so lifecycle paths and point mutations can keep the shadow cache synchronized from the live `meshNodes` records.
  - Rebuilds metadata after NodeDB shape changes: default install, load, reset, cleanup, removal, and eviction compaction.
  - Refreshes metadata after hot-field mutations such as user updates, position/telemetry presence changes, packet-heard updates, favorite/ignore/mute/key-verify writes, and local-node time refreshes.
  - Changed `sortMeshDB()` to compare `NodeMeta` entries instead of full `meshtastic_NodeInfoLite` records.
  - Changed hot read paths to consult metadata first while still returning full records from storage:
    - `getMeshNode(NodeNum)`
    - `getMeshNodeChannel(NodeNum)`
    - `isFavorite()`
    - `isFromOrToFavoritedNode()`
    - `getNumOnlineMeshNodes()`
    - `getLicenseStatus()`
  - Changed full-DB eviction scoring in `getOrCreateMeshNode()` to use metadata flags and `last_heard` instead of repeatedly inspecting the larger full records for those hot criteria.

### Important Constraints For Later Phases

- `meshNodes` is still the source of truth in phase 6. `NodeMeta` is a synchronized shadow cache, not an independent store.
- `nodeMeta` is currently storage-slot aligned. `displayOrder` still contains storage indices, which also happen to be metadata indices in this phase. If a later phase decouples those identities, do it deliberately and update both contracts together.
- Any NodeDB mutation that changes metadata-backed fields must refresh the corresponding `NodeMeta` entry before a later sort or metadata lookup relies on it.
- Phase 7 should build the `NodeNum -> meta index` hash over `nodeMeta`, not by reintroducing full-record scans over `meshNodes`.
- The metadata cache now carries `storage_index` as the hand-off back to the full record. Later slot-store phases should preserve that role even if the backing store stops being a plain vector.

### Verification

- `pio run -e tbeam-s3-core`
- `pio run -e rak4631`
- `pio run -e native` still fails before NodeDB changes are exercised in the existing Portduino/LovyanGFX macOS toolchain path. The current local failure remains the same class seen in earlier phases: LovyanGFX native sources hit missing C runtime declarations (`size_t`, `memcpy`, `memmove`, `strlen`) plus missing `malloc.h`.

### Follow-Up Notes

- Phase 6 stores `snr_q4`, `role`, and `hw_model` in metadata even though current hot paths do not consume them yet. That keeps phase 7+ hash and backend work from needing another metadata-shape expansion for those fields.
- Because `getMeshNode(NodeNum)` now searches `nodeMeta` and then projects back to storage, future work should treat storage-index scans as legacy behavior to be removed rather than as the default access pattern.

## Phase 7: Add `NodeNum -> meta index` Hash Lookup

Date: 2026-04-14
Status: Implemented

### Goal

Replace linear metadata scans with a private open-addressing hash while keeping `nodeMeta` as the compact hot-record source and `meshNodes` as the full-record store.

### What Changed

- `src/mesh/NodeDB.h`
  - Added a private `nodeMetaLookup` table that stores `NodeNum -> nodeMeta index` mappings with an open-addressing layout.
  - Added lookup-sizing and hash helpers plus a dedicated `rebuildNodeMetaLookup()` path.
- `src/mesh/NodeDB.cpp`
  - Rebuilt the lookup table after `rebuildNodeMeta()` finishes refreshing the live metadata array.
  - Updated `refreshNodeMeta()` to trigger a lookup rebuild when a slot's `node_num` identity changes, while leaving ordinary hot-field refreshes alone.
  - Changed `getNodeMeta()` to probe the hash table first instead of linearly scanning `nodeMeta`.
  - Collapsed `isFromOrToFavoritedNode()` onto two hashed metadata lookups instead of a whole-DB scan.

### Important Constraints For Later Phases

- `getMeshNode()` may be called from ISR-adjacent paths, so phase 7 intentionally keeps lookup reads allocation-free. Do not move hash rebuilds into `getNodeMeta()` or any other read path.
- The hash table stores metadata indices, not storage indices directly. Future slot-store phases should keep `NodeMeta` as the hand-off layer from `NodeNum` lookup to whatever full-record backend owns the slot.
- `refreshNodeMeta()` only rebuilds the hash when slot identity changes (`node_num` changes or a new slot appears). If a later phase introduces metadata relocation without a full `rebuildNodeMeta()`, it must also explicitly rebuild or incrementally maintain `nodeMetaLookup`.
- Duplicate `NodeNum` entries still resolve to the first metadata slot encountered during rebuild, matching the earlier linear-scan behavior. Later phases should treat duplicate live node numbers as a data-integrity bug, not as a lookup feature.

### Verification

- `pio run -e tbeam-s3-core`
- `pio run -e rak4631`
- `pio run -e native` is still expected to fail before NodeDB code is exercised in the existing Portduino/LovyanGFX macOS toolchain path (`malloc.h` and missing C runtime declarations in LovyanGFX sources).

### Follow-Up Notes

- Phase 8 can keep using `getNodeMeta()` as the stable hot lookup API while changing save/load internals, because the lookup no longer depends on scanning `meshNodes`.
- If phase 10 replaces the vector-backed store with a `NodeStore` shim, `nodeMetaLookup` should survive largely unchanged; only `meta.storage_index` projection needs to swap over to the new backend contract.

## Phase 8: Stream The `nodes.proto` Save Path

Date: 2026-04-14
Status: Implemented

### Goal

Stop `saveNodeDatabaseToDisk()` from serializing the entire padded `nodeDatabase.nodes` backing vector while keeping the existing `nodes.proto` schema intact.

### What Changed

- `src/mesh/NodeDB.cpp`
  - Added a small `encodeLiveNodeDatabase()` helper that writes the top-level `meshtastic_NodeDatabase` protobuf directly to an output stream.
  - Changed the NodeDB save path to emit the `version` field plus only the live `[0, numMeshNodes)` storage slots instead of encoding every element in the padded `nodeDatabase.nodes` vector.
  - Kept the on-disk protobuf format unchanged: the file still contains a `meshtastic_NodeDatabase` with repeated `NodeInfoLite` entries, but phase 8 no longer writes zeroed tail slots just to preserve capacity.
  - Left the decode callback path in place for phase 9, so load behavior is unchanged in this phase.

### Important Constraints For Later Phases

- The saved node count is now `numMeshNodes`, not `nodeDatabase.nodes.size()`. Do not reintroduce tail-slot serialization in later save-path or backend work.
- Phase 9 should continue to accept both older padded saves and newer live-only saves, since both use the same `nodes.proto` schema.
- `saveNodeDatabaseToDisk()` is now a special-case streamed writer instead of a `saveProto()` call. If a later phase introduces a `NodeStore` abstraction, preserve this streamed-write pattern or replace it with an equivalent backend-aware encoder.
- Save still walks live storage order, not `displayOrder`. Later phases should keep that distinction explicit.

### Verification

- `pio run -e tbeam-s3-core` succeeded locally.
- `pio run -e rak4631` succeeded locally.
- `pio run -e native` is still expected to fail before NodeDB changes are exercised in the existing Portduino/LovyanGFX macOS toolchain path (`malloc.h` and missing C runtime declarations in LovyanGFX sources).

### Follow-Up Notes

- Phase 8 removes save-side over-serialization but does not change load-side vector growth yet. Phase 9 still needs to decode one node at a time and avoid leaving legacy load capacity pinned longer than necessary.

## Phase 9: Stream The `nodes.proto` Load Path

Date: 2026-04-14
Status: Implemented

### Goal

Stop `nodes.proto` load from growing a temporary callback-owned `std::vector` while keeping the on-disk protobuf schema unchanged.

### What Changed

- `src/mesh/NodeDB.cpp`
  - Added a streamed `meshtastic_NodeDatabase` decoder that walks the top-level protobuf fields directly and decodes each `NodeInfoLite` submessage one at a time.
  - Changed `loadProto()` to special-case `meshtastic_NodeDatabase_msg` so `nodes.proto` no longer goes through the generic callback-based `pb_decode()` path that appended into `nodeDatabase.nodes`.
  - Replaced the load-time vector setup with a fresh fixed-size backing vector before decode, which explicitly drops any oversized capacity that a legacy callback-grown load could have left pinned.
  - Validates each decoded node before admission:
    - rejects reserved / broadcast node numbers
    - rejects records without `has_user`
    - rejects duplicate `NodeNum` entries after keeping the first occurrence
  - Preserved the existing zero-public-key scrub during load by clearing all-zero stored public keys before the node is admitted.
  - Forces `nodeDatabase.version = 0` on streamed decode failure so partially-decoded NodeDB contents do not survive as a seemingly valid load result.
  - Leaves compatibility intact for both older padded saves and newer live-only saves: repeated node records are still read from the same `meshtastic_NodeDatabase` protobuf schema, but zeroed tail entries are filtered instead of being admitted into the live count.

### Important Constraints For Later Phases

- Phase 9 keeps the existing `nodes.proto` file format. Later backend work should preserve compatibility or make any format break explicit and deliberate.
- The streamed load path now owns NodeDB admission rules for on-disk records. If a later phase broadens or narrows what counts as a valid persisted node, update this streamed decoder rather than reintroducing callback-vector decoding.
- `nodeDatabase.nodes.size()` after the streamed decode is now the live node count until `loadFromDisk()` resizes the backing vector back to `MAX_NUM_NODES`. That is intentional; do not assume the decoded vector arrives pre-padded anymore.
- Duplicate node numbers are now discarded at load time after the first admitted record. Future phases should treat duplicate persisted `NodeNum` entries as corruption, not as something the hot lookup layer should preserve.

### Verification

- `pio run -e tbeam-s3-core`
- `pio run -e rak4631`
- `pio run -e native` still fails before NodeDB-specific behavior is exercised in the existing Portduino/LovyanGFX macOS toolchain path. The local failure remains the same class seen in earlier phases: missing C runtime declarations in LovyanGFX C/C++ sources plus missing `malloc.h`.

### Follow-Up Notes

- Phase 9 removes the load-side callback growth path, but no dedicated runtime heap instrumentation was added in this phase. The current guarantee comes from the new fixed-size streamed decode path and the explicit reset of the backing vector before admission.
- Phase 10 can treat this streamed decode as the load-side behavioral contract to preserve while swapping `meshNodes` behind a `NodeStore` shim.

## Phase 10: Introduce `NodeStore` With A Shim Backed By Current Storage

Date: 2026-04-14
Status: Implemented

### Goal

Add the storage-backend seam without changing runtime behavior, persistence format, or the current vector-backed full-record model.

### What Changed

- `src/mesh/NodeStore.h`
  - Added a small `NodeStore` interface for slot-oriented full-record access, storage reset/resize, raw slot-data projection, and tail-slot clearing.
- `src/mesh/ArraySlotStore.h`
- `src/mesh/ArraySlotStore.cpp`
  - Added `ArraySlotStore` as the phase-10 shim implementation backed directly by `nodeDatabase.nodes`.
  - Kept the shim intentionally thin: it still uses the existing in-memory vector layout, but NodeDB now reaches that storage through a backend-shaped contract instead of assuming `std::vector` at every call site.
- `src/mesh/NodeDB.h`
  - Added private `slotAt()` / `slotPtr()` helpers plus `ArraySlotStore` / `NodeStore` members.
- `src/mesh/NodeDB.cpp`
  - Constructed NodeDB with an `ArraySlotStore` bound to the current `nodeDatabase.nodes` backing vector and made the store the internal slot-access seam.
  - Switched storage-touching lifecycle and access paths to use the store abstraction:
    - default NodeDB install
    - load-time resize back to `MAX_NUM_NODES`
    - reset / remove / cleanup compaction
    - metadata refresh and storage-index recovery
    - display-index reads and hashed `NodeNum -> full record` reads
    - full-DB eviction compaction and append-at-end node creation
  - Kept the public `meshNodes` pointer alive as a compatibility alias to the same vector-backed storage so untouched callers continue to build in this phase.
  - Left the phase-8/9 streamed `nodes.proto` save/load logic intact, so phase 10 is a pure storage-seam refactor rather than a persistence behavior change.

### Important Constraints For Later Phases

- `NodeMeta.storage_index` now means "slot index in the active `NodeStore`", not "index into a known `std::vector` implementation detail". Future backends should preserve that meaning.
- `ArraySlotStore` is only a shim in phase 10. Phase 11 should expand its allocation/backing behavior for PSRAM-capable targets without pushing vector assumptions back into NodeDB.
- The compatibility `meshNodes` pointer still exists for external callers, but phase 10 does not bless it as the long-term backend API. Later phases should continue migrating callers toward NodeDB-owned read helpers instead of adding new direct `meshNodes` dependencies.
- The streamed `nodes.proto` save/load contract from phases 8 and 9 remains authoritative. Do not regress to callback-grown load vectors or padded tail-slot saves while swapping backends underneath.
- Tail-slot clearing is now explicitly a store operation. If a later backend cannot cheaply zero unused slots in place, it still needs an equivalent way to make post-compaction unused capacity non-live and non-observable.

### Verification

- `pio run -e tbeam-s3-core` succeeded locally.
- `pio run -e rak4631` succeeded locally.
- `pio run -e native` still fails before NodeDB-specific behavior is exercised in the existing Portduino/LovyanGFX macOS toolchain path. The local failure remains in LovyanGFX native C sources (`lgfx_miniz.c`, `lgfx_pngle.c`, `lgfx_qrcode.c`, `lgfx_tjpgd.c`) due to missing C runtime declarations such as `memcpy`, `memset`, `memmove`, `memcmp`, `strlen`, and `size_t`.

### Follow-Up Notes

- Phase 10 intentionally keeps save/load helper code working directly against `nodeDatabase.nodes` because the protobuf object is still the persistence interchange format in this phase. Later backend phases can move more of that logic behind `NodeStore` only when the backend-specific persistence story is ready.
- Because `NodeDB::slotAt()` now centralizes internal full-record slot access, later phases can move reads and writes to PSRAM or flash by changing the store implementation instead of touching every NodeDB call site again.

## Phase 11: Enable PSRAM-Backed `ArraySlotStore`

Date: 2026-04-14
Status: Implemented

### Goal

Let PSRAM-capable `ESP32-S3` targets allocate a materially larger in-memory slot store while keeping non-PSRAM behavior unchanged.

### What Changed

- `src/mesh/mesh-pb-constants.h`
  - Split the old `ESP32-S3` flash-size heuristic into two paths:
    - a legacy no-PSRAM default cap (`100` / `200` / `250`)
    - a PSRAM-capable upper bound (`500` / `1000` / `3000`)
  - Added shared headroom constants for the phase-11 runtime-cap calculation:
    - reserved PSRAM headroom
    - reserved heap headroom
    - estimated DRAM overhead per live node
- `src/mesh/ArraySlotStore.h`
  - Added a private slot-cap helper so the array backend can clamp requested capacity before allocating.
- `src/mesh/ArraySlotStore.cpp`
  - Added the PSRAM-aware runtime cap calculation for `ESP32-S3 + BOARD_HAS_PSRAM`.
  - The runtime cap now takes the minimum of:
    - the compile-time upper bound from `MAX_NUM_NODES`
    - available PSRAM after a fixed reserve
    - available heap after a fixed reserve and per-node DRAM estimate
  - If PSRAM is unexpectedly unavailable at runtime on a PSRAM-configured `ESP32-S3`, the store falls back to the old no-PSRAM `ESP32-S3` cap instead of trying to keep the higher PSRAM target.
  - Added capacity logging so boots show the requested slot count, actual slot count, and current heap / PSRAM headroom.
- `src/mesh/NodeDB.cpp`
  - Changed the streamed save path to encode directly from the active `NodeStore` instead of assuming `nodeDatabase.nodes` is the authoritative full-record backing.
  - Changed load-time truncation and `isFull()` to use `nodeStore->slotCount()` rather than the compile-time `MAX_NUM_NODES` macro, so the runtime cap is now the real admission limit.
  - Added before/after NodeDB save/load memory snapshots on ESP32 builds to support phase-11 verification.

### Important Constraints For Later Phases

- After phase 11, `MAX_NUM_NODES` is a platform-specific upper bound, not the guaranteed live runtime capacity on PSRAM builds. New admission, truncation, or eviction code should use `nodeStore->slotCount()` when it needs the actual current cap.
- The phase-11 runtime cap is intentionally conservative and heuristic-driven. If later phases change `NodeMeta[]`, display-order storage, or heap-heavy subsystems, revisit the heap-overhead estimate instead of silently assuming the current numbers are still safe.
- Streamed NodeDB save now reads from `NodeStore`, not from `nodeDatabase.nodes`. Preserve that backend-oriented contract when flash-backed stores land; do not reintroduce save paths that depend on one particular in-memory container.
- Phase 11 still keeps the load-time protobuf shape unchanged. Phase 12+ flash work should preserve `nodes.proto` import behavior until an explicit migration path exists.

### Verification

- `pio run -e tbeam-s3-core` succeeded locally.
- `pio run -e rak4631` succeeded locally as a non-PSRAM regression check.

### Follow-Up Notes

- Phase 11 adds logging for heap / PSRAM snapshots around NodeDB save/load, but it does not yet persist or summarize those measurements anywhere outside the boot/runtime logs.
- Phase 12 can treat the new runtime-cap split as established: `ArraySlotStore` now owns "how many slots can this target really support right now?", while `NodeDB` consumes the resulting capacity through the generic `NodeStore` interface.

## Phase 12: Add `FlashSlotStore` Raw Record I/O

Date: 2026-04-14
Status: Implemented

### Goal

Land the flash-slot on-disk contract and low-level record I/O without changing NodeDB's active backend selection yet.

### What Changed

- `src/mesh/FlashSlotStore.h`
  - Added a standalone `FlashSlotStore` helper with the phase-12 on-disk structs:
    - a fixed-width manifest containing store magic/version, slot capacity, record size, and `slots_per_page`
    - a fixed-width slot-record header containing magic/version, presence flags, protobuf `encoded_len`, and CRC32
  - Declared `initialize()`, `readManifest()`, `writeManifest()`, `readSlot()`, and `writeSlot()` as the raw helper API for later flash-backend phases.
- `src/mesh/FlashSlotStore.cpp`
  - Implemented manifest read/write at `/prefs/nodedb/manifest.bin`.
  - Implemented page-file storage at `/prefs/nodedb/page-XXX.bin`, with page geometry derived from `slots_per_page * sizeof(Record)`.
  - Implemented `writeSlot()` by nanopb-encoding a single `meshtastic_NodeInfoLite` into the fixed-size payload buffer, storing the encoded length, and computing CRC32 over only the valid encoded bytes.
  - Implemented `readSlot()` to validate manifest shape, page bounds, record header, encoded length, and CRC before decoding the protobuf payload back into a scratch `meshtastic_NodeInfoLite`.
  - Chose whole-page rewrites for phase 12 so the helper works with the current cross-platform filesystem open modes without needing NodeDB integration or in-place read/write file handles yet.

### Important Constraints For Later Phases

- Phase 12 does not wire `FlashSlotStore` into `NodeDB`, backend selection, boot scan, or mutation paths yet. Phase 13 is the first phase that should make these raw helpers observable at runtime.
- The manifest is now the source of truth for flash-slot geometry. Later phases should validate against `record_size` and `slots_per_page` from the manifest instead of hard-coding those assumptions in boot/import paths.
- Slot payloads are protobuf-encoded `NodeInfoLite`, not raw struct bytes. Future migration/import code must preserve that contract so flash records stay compiler-layout independent.
- CRC32 is computed only across the first `encoded_len` payload bytes. Future corruption handling should keep rejecting records whose CRC or encoded length fails validation instead of trying to decode them anyway.
- Phase 12 currently rewrites an entire page file for each slot write. Later phases may optimize that if measurements justify it, but they should preserve the same on-disk manifest/record format unless an explicit migration step is added.

### Verification

- `pio run -e rak4631` succeeded locally.
- `pio run -e tbeam-s3-core` succeeded locally.

### Follow-Up Notes

- The page-file naming and manifest geometry are now concrete enough for phase 13 boot-scan work: `slotIndex -> pageIndex + page offset` no longer needs to be designed.
- Because `FlashSlotStore` is still standalone in phase 12, later phases are free to evolve the `NodeStore` abstraction separately if the current pointer-oriented interface is still too array-centric for flash-backed reads.

## Phase 13: Integrate `FlashSlotStore` For Boot Scan And Reads

Date: 2026-04-14
Status: Implemented

### Goal

Make the flash-slot read path real without pulling phase-14 migration and flash-write ownership into the same diff.

### What Changed

- `src/mesh/NodeDB.h`
  - Added the phase-13 flash-read state:
    - a `FlashSlotStore` member
    - a `flashSlotStoreActive` flag
    - a dense `storage_index -> flash slot index` map
    - per-slot cache-valid bits for the existing RAM-side compatibility overlay
  - Added private helpers for flash-backed boot scan, lazy slot hydration, dense-slot moves, and cache clearing.
- `src/mesh/NodeDB.cpp`
  - Added `loadFromFlashSlotStore()` to scan the flash manifest and rebuild `NodeMeta[]`, `displayOrder`, and the phase-7 hash directly from flash records without first bulk-loading them into `nodeDatabase.nodes`.
  - Added `slotAt()` / `slotPtr()` lazy hydration for flash-backed boots:
    - dense storage indices remain the NodeDB-facing identity
    - the first full-record read for a live node pulls that node's flash slot into the matching RAM overlay slot
    - later reads and in-boot mutations reuse the RAM copy
  - Added dense-slot move / clear helpers so compaction and eviction keep the flash-slot mapping aligned with storage-order changes.
  - Changed `saveNodeDatabaseToDisk()` to force-load all live flash-backed slots before streaming `nodes.proto`, so the old persistence path still writes a complete legacy file.
  - Changed boot selection to try legacy `/prefs/nodes.proto` first and only fall back to flash-slot boot scan on targets that prefer flash-backed reads when the legacy protobuf load is unavailable or invalid.
  - Skipped the constructor-time `cleanupMeshDB()` compaction pass after a flash boot scan because phase-13 flash admission already rejects no-user / duplicate / reserved nodes and scrubs all-zero public keys.

### Important Constraints For Later Phases

- Phase 13 does **not** make flash slots the authoritative persisted source when a valid legacy `nodes.proto` still exists. That is intentional: phase 14 still owns migration and write-path cutover.
- Once phase 13 boots from flash, full-record reads go through the flash-backed lazy overlay, but durable saves still write `nodes.proto`. Future work must not assume flash records stay current after an in-boot mutation until phase 14 lands.
- `NodeMeta.storage_index`, `displayOrder`, and the open-addressed hash still operate on dense NodeDB storage indices, not raw flash slot numbers. The `flashSlotByStorageIndex` map is now the translation layer future phases must preserve.
- The RAM overlay remains fixed-size in phase 13 for pointer compatibility with existing callers. This phase improves boot/read plumbing, not the final RAM-footprint story.

### Verification

- `pio run -e rak4631`
- `pio run -e tbeam-s3-core`

### Follow-Up Notes

- Phase 14 should flip flash-backed boots from "read fallback with legacy save handoff" to "authoritative backend with migration and write-through".
- If a later phase wants to reduce the fixed RAM overlay size, it must first finish the remaining pointer-lifetime cleanup for callers that still rely on stable `meshtastic_NodeInfoLite *` addresses.

## Phase 14: Integrate `FlashSlotStore` Writes And Migration

Date: 2026-04-14
Status: Implemented

### Goal

Make flash slots the authoritative persisted NodeDB backend on flash-preferred targets while safely migrating legacy `nodes.proto` data forward.

### What Changed

- `src/mesh/FlashSlotStore.h`
  - Added `clearSlot()` so NodeDB can explicitly remove stale slot records after compaction, reset, or migration writes.
- `src/mesh/FlashSlotStore.cpp`
  - Added page-existence checks plus a shared record-write helper so slot writes and slot clears reuse the same page rewrite path.
  - Implemented `clearSlot()` by zeroing an existing record in-place while skipping nonexistent pages, which avoids creating empty page files just to represent absence.
- `src/mesh/NodeDB.cpp`
  - Changed flash-preferred `saveNodeDatabaseToDisk()` behavior to write the live dense NodeDB view directly into flash slots instead of streaming `/prefs/nodes.proto`.
  - Added full-store verification after flash writes:
    - manifest slot count must match the runtime slot count
    - every live slot must read back and re-encode identically
    - every tail slot beyond `numMeshNodes` must read back as absent
  - Deleted legacy `/prefs/nodes.proto` only after the flash write passes verification.
  - Added phase-14 boot migration: when a flash-preferred build successfully loads a legacy `nodes.proto`, NodeDB now immediately writes and verifies the flash-slot store, then removes the legacy file.
  - Changed the constructor’s “first save” check to look for a flash manifest on flash-preferred builds instead of treating missing `/prefs/nodes.proto` as “never persisted”.
  - When running from a flash-backed boot, post-save flash slot mappings are normalized back to `storage_index == flash slot index` so later compaction and read paths stay aligned with the rewritten on-disk layout.

### Important Constraints For Later Phases

- Phase 14 keeps the phase-13 runtime split during migration boots: if the device booted from legacy `nodes.proto`, that boot still runs from the array-backed in-memory state, but every durable save now targets flash slots. The next reboot is what switches read bootstrapping over to flash.
- On flash-preferred targets, `/prefs/nodes.proto` is now migration input only. Later phases should not restore it as the primary persisted NodeDB source.
- Flash-slot verification is intentionally strict before deleting legacy state. If later phases change slot geometry or record encoding, they must preserve an equivalent “verify destination before removing source” rule.
- Tail-slot clearing now matters for correctness, not just hygiene. Deleted or compacted nodes must stay absent on reboot, so later flash-write changes must preserve the explicit clear path for slots beyond `numMeshNodes`.

### Verification

- `pio run -e rak4631`
- `pio run -e tbeam-s3-core`

### Follow-Up Notes

- Phase 14 still rewrites the full flash-backed NodeDB on each durable save. That is acceptable for the current cutover, but phase 16’s optional dirty-slot cache remains the next obvious optimization point if wear or latency becomes measurable.
- The flash-store verification helper compares nanopb encodings, not raw struct bytes. Keep that distinction if later phases touch `NodeInfoLite` packing or compiler-specific layout concerns.

## Phase 15: Capacity Enablement And Final Cap Updates

Date: 2026-04-14
Status: Implemented

### Goal

Finalize the platform target caps only after the stable-slot storage model and flash-backed backend are in place.

### What Changed

- `src/mesh/mesh-pb-constants.h`
  - Added `NODEDB_TARGET_CAP` as the explicit build upper bound for NodeDB capacity while keeping `MAX_NUM_NODES` as the compatibility macro used by the rest of the codebase and by Portduino overrides.
  - Replaced the older board-flash-size heuristics with the phase-15 target caps:
    - `ESP32-S3 + PSRAM`: `3000`
    - no-PSRAM `ESP32` / `ESP32-S3`: `500`
    - `nRF52`: `300`
    - `STM32WL`: unchanged at `10`
  - Kept the phase-11 PSRAM headroom constants in place so runtime slot allocation on PSRAM hardware still clamps below the build target when heap or PSRAM headroom is insufficient.
- `src/mesh/ArraySlotStore.cpp`
  - Changed the PSRAM-capable `ESP32-S3` fallback path so boards that were compiled for PSRAM but boot without usable PSRAM now clamp to the finalized no-PSRAM target cap of `500` instead of the older flash-size-derived values.

### Important Constraints For Later Phases

- After phase 15, `NODEDB_TARGET_CAP` is the intended name for the build-time upper bound. `MAX_NUM_NODES` still exists only as a compatibility alias; future NodeDB-cap work should prefer the newer term when touching cap-selection logic or documentation.
- The phase-11 runtime-cap logic still matters on PSRAM builds. `ESP32-S3 + PSRAM` now targets `3000`, but actual live capacity is still `min(NODEDB_TARGET_CAP, heap budget, PSRAM budget)` at boot.
- Flash-preferred targets still keep the fixed RAM overlay introduced in phases 13-14. Raising the no-PSRAM target cap to `500` therefore intentionally increases the overlay budget; later phases should not assume the flash backend is “free” in DRAM just because the durable store lives on flash.
- Portduino continues to supply its own `MAX_NUM_NODES` override through variant config. The new `NODEDB_TARGET_CAP` macro intentionally inherits that override so native test configurations keep working without another variant migration in phase 15.

### Verification

- `pio run -e tbeam-s3-core`
- `pio run -e heltec-v3`
- `pio run -e tbeam`
- `pio run -e rak4631`

### Follow-Up Notes

- Phase 15 finalizes the current target caps but does not add synthetic node-population tooling or automated memory telemetry assertions yet. Those remain manual validation tasks if future cap increases are proposed.
- If future work changes the fixed flash-overlay behavior, revisit whether the no-PSRAM `500` target should stay uniform across all ESP32-family builds or become backend-/board-specific again.

## Phase 16: Flash Dirty-Slot Flush

Date: 2026-04-14
Status: Implemented

### Goal

Stop the flash-backed NodeDB save path from rewriting the full dense store on every durable node change while keeping the existing full-rewrite path for layout changes, migration, and recovery.

### What Changed

- `src/mesh/NodeDB.h`
  - Added flash-save tracking state:
    - a per-storage-slot dirty bitmap
    - a conservative `flashSlotFullRewriteRequired` flag
  - Added private helpers for marking dirty slots, clearing dirty ranges, sizing the tracking bitmap, and forcing a full rewrite when storage layout changes.
- `src/mesh/NodeDB.cpp`
  - Marked flash-backed slots dirty whenever a live slot's metadata is refreshed outside a full metadata rebuild, which covers the existing NodeDB-owned mutation paths without changing their public API.
  - Marked storage moves and slot clears as layout-dirty so compaction, reset, removal, and eviction still take the safe full-rewrite path instead of trying to incrementally preserve shifted dense ordering.
  - Changed flash save behavior into two paths:
    - full rewrite when booting from legacy `nodes.proto`, normalizing a non-dense flash scan, changing slot layout, or reinitializing the manifest
    - incremental flush when only existing live slots are dirty
  - Added per-slot verification helpers so incremental saves verify only the slots they rewrote, while full rewrites still verify the whole store including cleared tail slots.
  - Changed `clearLocalPosition()` to refresh NodeDB metadata so later NodeDB saves still persist that local-record mutation through the dirty-slot path.
  - When boot scanning flash-backed state, the next save is now forced to rewrite if:
    - live records were admitted from non-identity flash slots
    - any records were rejected
    - any records were truncated
- `src/modules/AdminModule.cpp`
  - Replaced the fixed-position admin path's direct local-node writes with `nodeDB->updatePosition(..., RX_SRC_LOCAL)` so the existing NodeDB-owned mutation path marks the slot dirty before `saveChanges(SEGMENT_NODEDATABASE | SEGMENT_CONFIG)`.

### Important Constraints For Later Phases

- Phase 16 only makes ordinary record updates incremental. Any operation that changes dense storage layout still forces a full rewrite on the next flash save.
- The dirty bitmap is keyed by current dense storage index, not by `NodeNum` or raw flash slot number. If a later phase changes dense-slot identity again, it must update the dirty-tracking contract together with `flashSlotByStorageIndex`.
- Incremental verification is intentionally scoped to rewritten slots. Full-store verification still exists, but it now only runs when phase 16 takes the full-rewrite path.
- The low-level flash helper still rewrites an entire page file for each slot update. Phase 16 reduces how many slot writes happen per save; it does not add a page cache or batched page writer yet.

### Verification

- `pio run -e tbeam-s3-core`
- `pio run -e heltec-v3`
- `pio run -e rak4631`

### Follow-Up Notes

- The main remaining flash-write optimization is page-level batching: multiple dirty slots in the same page still cause multiple page rewrites during one save.
- Phase 16 assumes NodeDB-owned mutators or metadata refreshes remain the way durable node record edits are recorded. If a future phase adds new direct writer paths, it must either route them through NodeDB helpers or explicitly mark the affected slots dirty.

## Phase 17: Bugs And Cleanup

Date: 2026-04-14
Status: Implemented

### Goal

Address the review-driven correctness issue in flash-backed slot hydration and land low-risk cleanup that reduces avoidable flash manifest churn in the current batch paths.

### What Changed

- `src/mesh/NodeDB.h`
  - Removed the unused private `const slotAt()` overload now that internal slot access stays on the non-const helper.
- `src/mesh/NodeDB.cpp`
  - Fixed `slotAt()` so `ensureFlashSlotLoaded()` always runs before the debug assertion, which keeps release builds from skipping lazy flash hydration entirely.
  - Extracted shared loaded-node admission logic so both protobuf load and flash boot scan now use one helper for:
    - broadcast / reserved-node rejection
    - `has_user` requirement
    - all-zero public-key scrubbing
  - Reused one validated flash manifest for flash boot scan, full flash rewrite, and flash verification loops instead of reopening and revalidating the manifest on every slot operation in those batch paths.
- `src/mesh/FlashSlotStore.h`
  - Added manifest-aware `readSlot()`, `writeSlot()`, and `clearSlot()` overloads so batch callers can reuse a validated manifest.
- `src/mesh/FlashSlotStore.cpp`
  - Kept the existing one-off slot API as simple wrappers that read the manifest once and delegate to the manifest-aware overloads.
  - Left the whole-page record rewrite strategy unchanged.

### Important Constraints For Later Phases

- Phase 17 fixes the release-build hydration bug, but it does not change the current `slotAt()` contract beyond making the existing lazy load happen unconditionally.
- Manifest reuse now removes repeated manifest opens inside current batch paths, but it does not add persistent manifest caching to the store object itself.
- Phase 17 is intentionally not the page-batching phase. Multiple dirty slots that share one flash page still cause multiple page rewrites during a save.

### Verification

- `pio run -e tbeam-s3-core`
- `pio run -e heltec-v3`
- `pio run -e rak4631`

### Follow-Up Notes

- The next meaningful flash-write optimization is still page-level batching; phase 17 only removes redundant manifest reads from batch operations.
- Legacy `nodes.proto` import and flash-backed runtime read behavior were preserved by construction, but they were not separately hardware-exercised in this phase beyond the build matrix.

## Phase 18: Flash Page Batching

Date: 2026-04-14
Status: Implemented

### Goal

Stop flash-backed NodeDB saves from rewriting the same page once per dirty slot, and delete fully empty tail pages during dense rewrites instead of writing zero-filled page files.

### What Changed

- `src/mesh/FlashSlotStore.h`
  - Added minimal page-image helpers so `NodeDB` can batch writes by page without adding a persistent cache:
    - load one page
    - patch or clear one slot inside an in-memory page
    - store one page
    - delete one page file
- `src/mesh/FlashSlotStore.cpp`
  - Reused the existing record encoding and page layout rules behind the new page-image helpers.
  - Preserved the existing one-off slot APIs for lazy reads and non-batched callers.
  - Kept missing page files equivalent to all-cleared slots.
- `src/mesh/NodeDB.cpp`
  - Changed full flash rewrites to build one zeroed page image per live page, patch every live slot for that page, and write that page once.
  - Changed fully empty trailing pages to be removed instead of rewritten as all-zero page files.
  - Changed incremental dirty-slot saves to:
    - hydrate dirty storage slots before batching
    - group dirty flash writes by page index
    - load each touched page once
    - patch all dirty slots for that page in memory
    - write the page once
    - re-read the page once for page-local verification before clearing dirty bits
  - Added save logs that report dirty-slot and touched-page counts so flash-write reduction is visible in normal debugging.

### Important Constraints For Later Phases

- Phase 18 keeps batching scoped to a single `saveNodeDatabaseToDisk()` call. It does not add a persistent page cache, deferred write-back, or background flushing.
- The on-disk manifest, record format, and lazy runtime hydration behavior are unchanged.
- One-off slot writes still use the simpler per-slot path. Phase 18 only batches the flash save flows owned by `NodeDB`.

### Verification

- `pio run -e tbeam-s3-core`
- `pio run -e heltec-v3`
- `pio run -e rak4631`

### Follow-Up Notes

- Phase 18 removes repeated page rewrites during one NodeDB save, but it does not yet add any broader store-level batching API for other callers.
- Legacy `nodes.proto` import and flash normalization should continue to behave the same, with phase 18 only changing how many page files get touched during the resulting save.
