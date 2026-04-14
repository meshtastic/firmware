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
