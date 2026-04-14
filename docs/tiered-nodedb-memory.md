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
