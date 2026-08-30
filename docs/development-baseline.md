# Development Baseline

## 1. Firmware Repository Path

`C:\Users\JPJYR\Documents\meshtastic`

## 2. Firmware Remote

`origin https://github.com/meshtastic/firmware.git`

## 3. Firmware Baseline Commit

`7239fe886a30fa13cd35946fa5ae1a46a2807eeb`

This is the current `HEAD` inspected during Phase 0.

## 4. Firmware Baseline Branch/Tag

Current branch during inspection: `develop`.

Tag pointing at current commit:

- `v2.8.0.7239fe8`

No baseline branch or new tag was created because the working tree is not clean. Untracked local preparation/build outputs are present, including `.pio-core/`, `dist/`, build logs, and documentation files.

Recommended future baseline name after review:

- Branch: `baseline/heltec-v4-r8-tft-2.8.0-working`
- Local tag: `heltec-v4-r8-tft-known-good-2.8.0`

Create these only after deciding what to do with untracked files.

## 5. Device UI Repository Path

Editable local clone prepared at:

`C:\Users\JPJYR\Documents\device-ui`

## 6. Device UI Remote

`origin https://github.com/meshtastic/device-ui.git`

## 7. Device UI Exact Baseline Commit

`9d9b9df81fcde646811a10942d00d5f45f72af7b`

Local Device UI branch created:

`feature/heltec-r8-quick-messages`

No source files were modified in this repository.

## 8. Current PlatformIO Dependency Mechanism

Firmware currently pulls Device UI through `[device-ui_base]` in root `platformio.ini`.

Current declaration:

```ini
[device-ui_base]
lib_deps =
	# renovate: datasource=git-refs depName=meshtastic/device-ui packageName=https://github.com/meshtastic/device-ui gitBranch=master
	https://github.com/meshtastic/device-ui/archive/9d9b9df81fcde646811a10942d00d5f45f72af7b.zip
```

The active cached copy appears under:

`.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/`

That directory is generated dependency state and must not be the permanent editable source.

## 9. Known-Good Target

`heltec-v4-r8-tft`

## 10. Known-Good Flash Mode

`--flash-mode dio`

Do not change this to `qio` unless later physical testing proves a safe change.

## 11. LittleFS Offset

`0xc90000`

Known full clean flash map:

```text
0x0       firmware-heltec-v4-r8-tft-2.8.0.7239fe8.factory.bin
0xc90000  littlefs-heltec-v4-r8-tft-2.8.0.7239fe8.bin
```

## 12. Known-Good Artifact Hashes

Existing artifacts found in `dist/heltec-v4-r8-tft/`:

```text
138FDEB29C5D3D655113ACAE2522328583C8B625D09A8CC5A7FEC816AC7C0830  firmware-heltec-v4-r8-tft-2.8.0.7239fe8.factory.bin
057092CD73FF7C2020CF9D15E471054E3FE0BA24BD1C825BD600E49B0E64C2AB  littlefs-heltec-v4-r8-tft-2.8.0.7239fe8.bin
0BF3CAC43B0C5D14482B11FF199DEFD1706D99ADE22CECE5202C0579D2DBE655  firmware-heltec-v4-r8-tft-2.8.0.7239fe8.bin
FDCE35E146F7C69A413B29CCD124E26099EECEB6511A90172FB2D6130FC5E636  meshtastic-heltec-v4-r8-tft-esptool-package.zip
```

The existing flashing scripts still contain `--flash-mode dio`.

## 13. Reproducible Build Procedure

Baseline verification command used:

```powershell
$env:PLATFORMIO_CORE_DIR="C:\p"
$env:PYTHONUTF8="1"
$env:PYTHONIOENCODING="utf-8"
$env:Path=(Resolve-Path ".\.venv\Scripts").Path + ";" + $env:Path
.\.venv\Scripts\pio.exe run -e heltec-v4-r8-tft -t mtjson -j 2 2>&1 | Tee-Object build-heltec-v4-r8-tft-baseline-verify-20260830.log
```

Verification result:

```text
Environment       Status    Duration
heltec-v4-r8-tft  SUCCESS   00:09:17.053
```

Tooling recorded:

- PlatformIO Core: `6.1.19`
- Python: `3.14.3`
- Platform: `espressif32 @ 55.3.311`
- Arduino ESP32: `3.3.11`
- ESP-IDF: `5.5.5`
- esptool: `5.3.0`
- LovyanGFX: `1.2.27`
- lvgl from Device UI dependency: `9.3.0`

Build output reported firmware version:

`2.8.0.7239fe8`

## 14. Development Branch Workflow

Recommended firmware branches:

- Keep current known-good commit recoverable through `v2.8.0.7239fe8` and a future local baseline branch/tag.
- Create feature work on `feature/heltec-r8-quick-messages`.
- Create separate experiments on branches such as `experiment/heltec-r8-ble-mui`.
- Create upstream update work on separate integration branches.

Do not mix dependency setup, Quick Message UI, send-helper refactoring, incoming-message emphasis, and BLE experiments into one commit.

## 15. Device UI Local-Development Workflow

Edit Device UI in:

`C:\Users\JPJYR\Documents\device-ui`

Current local branch:

`feature/heltec-r8-quick-messages`

During development, firmware may temporarily point to this local checkout using a PlatformIO local path dependency on a dedicated firmware feature branch. Final reproducible builds should point to an exact Device UI commit, not a moving branch or unpinned local path.

## 16. Rollback Procedure

To return firmware to the known-good dependency model:

1. Ensure `platformio.ini` `[device-ui_base]` points back to:

```text
https://github.com/meshtastic/device-ui/archive/9d9b9df81fcde646811a10942d00d5f45f72af7b.zip
```

2. Build `heltec-v4-r8-tft`.
3. Flash only with `--flash-mode dio`.
4. Use the known-good artifacts from `dist/heltec-v4-r8-tft/` if the new build is suspect.

Never use destructive git cleanup commands unless explicitly approved.

## 17. Rules for Codex Before Modifying Code

Before modifying code, Codex must run or review:

```powershell
.\tools\git-preflight.ps1
```

Codex must not commit, push, force-push, stash, reset, clean, merge, or rebase without explicit user instruction.

No firmware behavior, Device UI behavior, display/touch behavior, Bluetooth behavior, or generated UI output should be changed during repository preparation.

