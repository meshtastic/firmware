# Copilot instructions — Meshtastic Firmware

This file collects concise, actionable information an AI coding agent needs to be productive in this repository.

Core intent
- Repo: firmware for Meshtastic (LoRa mesh node firmware). Primary languages: C/C++ (Arduino/PlatformIO). Build system: PlatformIO.
- High-level structure: `src/` (application), `mesh/` (mesh stack + generated protobuf headers under `src/mesh/generated`), `modules/` (feature modules), `input/`, `graphics/`, `boards/` + `variants/` (device-specific configs), and `bin/` (helper scripts).

Quick build & dev workflows (what to run)
- Preferred local build: PlatformIO. `platformio.ini` sets `default_envs = tbeam` and imports `variants/*` and `arch/*` via `extra_configs`.
  - Build: `platformio run -e tbeam` (or use VS Code task "PlatformIO: Build").
  - Upload: `platformio run -e <env> -t upload`.
  - Monitor serial: `platformio device monitor -b 115200` (monitor_speed is 115200 in `platformio.ini`).
- High-level helper scripts in `bin/`:
  - `bin/build-firmware.sh` dispatches to architecture-specific scripts (`bin/build-esp32.sh`, `bin/build-nrf52.sh`, ...).
  - `bin/platformio-custom.py` is executed by PlatformIO: it sets build macros, generates combined ESP32 factory images, copies TFT boot logos, and converts nRF hex -> UF2. Inspect it before changing build flags.
  - `bin/regen-protos.sh` (and related scripts) regenerate protobuf C headers in `src/mesh/generated/` — do this after editing `.proto` files.

Important build-time configuration patterns
- Feature flags and configuration are often compile-time macros. Typical macros: `MESHTASTIC_EXCLUDE_*`, `HAS_TFT`, `HAS_BUTTON`, etc. Many are defined in `platformio.ini`'s `build_flags` or added from `userPrefs.jsonc` by `platformio-custom.py`.
  - To change a runtime option that is a compile macro, edit `userPrefs.jsonc` or `platformio.ini` and rebuild.
- `platformio-custom.py` appends `-DAPP_VERSION`, `-DBUILD_EPOCH`, and transforms `userPrefs.jsonc` entries into `-D` defines — be aware edits to those files change ABI/behavior and require a full rebuild.

Repository conventions and patterns to follow
- Multi-architecture support: code uses `#ifdef ARCH_...` and `extra_configs` to include board-specific INI fragments. To add a board, add a JSON in `boards/` and a variant INI under `variants/`.
- Radio abstraction: radio drivers implement `<Radio>Interface.h` and the code uses a common API via `Radio.getRadio()` / `RadioInterface` in `src/mesh/`. Look at `src/mesh/RadioInterface.h` and `src/RadioLibInterface.*` for examples.
- Module and event wiring: many subsystems register observers and threads. Example: `src/input/ButtonThread.cpp` registers with `InputBroker` and dispatches `InputEvent` via `notifyObservers(&evt)`; follow this pattern when adding input sources.
- Generated code: protobuf sources live in `protobufs/` and generated C headers are in `src/mesh/generated/meshtastic/`. Regenerate them when protos change.

Integration points & external dependencies
- External libs are managed via PlatformIO `lib_deps` (see `platformio.ini`). Many dependencies are git zips. CI may cache these; local builds fetch them.
- Device UI is a dependency (`meshtastic/device-ui`) and is wired into builds via `device-ui_base` and conditional `HAS_TFT` macros.
- Protobuf + nanopb: used for mesh messages. Look for `Nanopb` in `lib_deps` and `mesh/generated/` for the generated types.

Examples (concrete code pointers)
- Add a button handler: see `src/input/ButtonThread.cpp` — attach callbacks via `OneButton` and forward events as `InputEvent` to `InputBroker`.
- Add a radio type: see `src/LLCC68Interface.h` and `src/SX1262Interface.h` for driver templates and how they are constructed and registered in `main.cpp`.
- Add a variant/board: add JSON under `boards/` and a `variants/<vendor>/<board>/platformio.ini`. `platformio.ini` already pulls these with `extra_configs`.

Common pitfalls / tips for agents
- If you change compile-time macros, run a clean build. PlatformIO caches object files; inconsistent `-D` flags can cause weird behavior.
- `platformio-custom.py` mutates build flags at build time. Read it if a flag appears to come from nowhere — many runtime choices are actually compile-time.
- Boot logos (TFT) are copied from `branding/logo_<WxH>.png` into `data/boot` during build when `HAS_TFT` is defined — update or add images there.
- The project intentionally excludes many RadioLib modules via `-DRADIOLIB_EXCLUDE_*` to reduce build time/size. Do not remove those unless required.

Contract for code changes (quick checklist for agents)
- Inputs: change description, target architecture/variant, any new compile-time flags.
- Outputs: source files and updated `variants` or `boards` if adding hardware; a regenerated `src/mesh/generated/` entry if protobufs changed; updated `platformio.ini` only if global defaults change.
- Error modes: build failures from PlatformIO (missing libs or linker errors) and runtime missing-define behavior from wrong `-D` flags.

Where to look first
- `src/main.cpp` — application entry, module/driver initialization and wiring.
- `platformio.ini` and `bin/platformio-custom.py` — build config and how flags are derived.
- `src/mesh/`, `src/modules/`, and `src/input/` — common extension points.
- `protobufs/` and `src/mesh/generated/` — protobuf workflow.

If something is missing in this file, tell me what area to expand (build, variant wiring, radio drivers, protobuf workflow, or examples) and I will iterate.
