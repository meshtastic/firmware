# DBZ Radar — Meshtastic firmware fork

## Project goal
Custom Meshtastic firmware for the Seeed Wio Tracker L1 Pro. Add a "DBZ Radar"
screen frame that shows nearby nodes as dots, by bearing and distance from this
device, with selectable zoom ranges. A standalone friend-finder — no phone/app
needed at runtime.

## Hardware (target device)
- MCU: Nordic nRF52840
- LoRa: SX1262 (915 MHz / US region)
- GPS: Quectel L76K
- Display: 128x64 MONOCHROME OLED  <-- small + one color; design around this
- Input: 4-way joystick
- Build output: .uf2 file, flashed via double-tap-reset USB-drive method
- NO magnetometer/compass on this board

## Hardware-driven design constraints (important)
- Monochrome screen: distinguish nodes by SHAPE/glyph + single-char label,
  NOT by color.
- No compass: the radar is NORTH-UP. Center arrow is fixed pointing up (north)
  and represents this device's position, not its heading. Do not try to make
  the arrow follow heading.
- Tiny screen: usable radar radius is ~30px. Keep ring labels small and offset;
  expect to iterate on text placement for readability.
- Consumer GPS accuracy ~3-5m at best; worse indoors/in crowds.

## Build environment
- Build system: PlatformIO.
- Board PlatformIO env name: <FILL IN — first task is to find this in
  platformio.ini and variants/>
- Build command: pio run -e <env>
- Firmware output path: .pio/build/<env>/firmware.uf2

## Key code locations
- Node database (positions, names, IDs): src/mesh/NodeDB.cpp / .h
- Screen / UI frames (the pages cycled with the joystick): src/graphics/
  (start with Screen.cpp; UI may be split across sibling files in this version)
- Position broadcast logic: src/modules/PositionModule.cpp
- Channels / PSK: src/mesh/Channels.cpp

## Joystick mapping (must respect)
- LEFT / RIGHT = flip between pages/frames (existing stock behavior — keep it).
- UP / DOWN = change radar zoom level WHILE on the radar frame.
- Never put zoom on left/right (it would collide with page-flip).

## Working rules (follow every session)
- Build after EVERY change. Never leave the tree non-compiling.
- Fix all compile errors before moving to the next stage.
- Small, single-purpose commits with clear messages.
- Always leave the build in a flashable state.
- This is ADDITIVE: do not modify or remove existing frames. Add new ones.
- Add serial debug logging (readable via `pio device monitor`) for node
  distances, bearings, counts, and computed x/y — I verify correctness on the
  laptop separately from judging legibility on the OLED.
- I (the human) handle all flashing, GPS fixes, and real-world testing. You
  cannot see the screen or flash the board — when I report what the device did,
  use that as ground truth.
- If something is uncertain in this firmware version (NodeDB field names,
  joystick event handling, frame-list API), tell me explicitly rather than
  guessing silently.

## Version discipline
- Pinned to a stable release tag on branch `dbz-radar`. Stay on it while
  developing; don't pull moving upstream mid-project.
- Keep a small on-screen version string ("DBZ vX.Y") so I always know which
  build is flashed. Bump it when you change the radar meaningfully.

## MVP scope (do not exceed without asking)
- CURRENT build: a working radar frame rendering LIVE nodes from the NodeDB,
  ALL channels (public/LongFast nodes appearing is EXPECTED, not a bug),
  capped to the 12 nearest.
- DEFERRED (do NOT build yet unless I ask):
  - Group/channel display-filter (show only my private group).
  - Adaptive position broadcast rate (slow idle / fast on-demand).
  - In-field device pairing.

## Notes
- Firmware is GPLv3; if ever distributed, source must be published and it
  cannot be branded "Meshtastic". My product name is "DBZ Radar".
- Keep any real channel PSK OUT of committed code if I add one later.
