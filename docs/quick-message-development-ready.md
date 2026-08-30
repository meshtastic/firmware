# Quick Message Development Ready State

## 1. Firmware baseline

Known-good upstream firmware source baseline:

- Commit: `7239fe886a30fa13cd35946fa5ae1a46a2807eeb`
- Official tag: `v2.8.0.7239fe8`
- Target: `heltec-v4-r8-tft`
- Known working flash mode: `dio`
- LittleFS offset: `0xc90000`

## 2. Repository preparation commit hash

Repository-preparation commit:

`6989949e99d156e4be392a08602e0d9d7c35b3d9`

Short form:

`6989949e9 chore: establish Heltec R8 development baseline`

This commit contains only non-functional repository preparation:

- `.gitignore`
- `GIT-POLICY.md`
- `docs/development-baseline.md`
- `docs/device-ui-development-workflow.md`
- `docs/git-hygiene-review.md`
- `tools/git-preflight.ps1`

## 3. Firmware feature branch

Current firmware branch:

`feature/heltec-r8-quick-messages`

Current firmware `HEAD`:

`6989949e99d156e4be392a08602e0d9d7c35b3d9`

## 4. Device UI feature branch

Device UI repository:

`C:\Users\JPJYR\Documents\device-ui`

Branch:

`feature/heltec-r8-quick-messages`

## 5. Device UI exact commit

`9d9b9df81fcde646811a10942d00d5f45f72af7b`

No Device UI source changes have been made.

## 6. Local Device UI path

`C:\Users\JPJYR\Documents\device-ui`

Relative path from firmware repository:

`../device-ui`

## 7. PlatformIO dependency method used

On firmware branch `feature/heltec-r8-quick-messages`, `[device-ui_base]` in `platformio.ini` uses a local path dependency:

```ini
[device-ui_base]
lib_deps =
	# renovate: datasource=git-refs depName=meshtastic/device-ui packageName=https://github.com/meshtastic/device-ui gitBranch=master
	# DEVELOPMENT ONLY for feature/heltec-r8-quick-messages:
	# use the editable local checkout at C:\Users\JPJYR\Documents\device-ui.
	# Final reproducible builds must switch back to an exact pinned commit archive.
	../device-ui
```

This is intentionally uncommitted at the time this report was written unless the user later approves committing the local-development dependency switch.

## 8. Proof local Device UI was used by build

PlatformIO dependency graph reported:

```text
meshtastic-device-ui @ 1.0.0 (required: file://../device-ui)
```

During dependency resolution, PlatformIO also reported:

```text
Library Manager: Installing file://../device-ui
```

This confirms the build used the local editable Device UI checkout rather than the pinned remote archive.

## 9. Build result

Command used:

```powershell
$env:PLATFORMIO_CORE_DIR="C:\p"
$env:PYTHONUTF8="1"
$env:PYTHONIOENCODING="utf-8"
$env:Path=(Resolve-Path ".\.venv\Scripts").Path + ";" + $env:Path
.\.venv\Scripts\pio.exe run -e heltec-v4-r8-tft -t mtjson -j 2 2>&1 | Tee-Object build-heltec-v4-r8-tft-local-device-ui-20260830.log
```

PlatformIO result:

```text
heltec-v4-r8-tft  SUCCESS  00:23:19.084
```

Firmware version reported by build:

`2.8.0.6989949`

The version differs from `2.8.0.7239fe8` because the firmware `HEAD` now includes the repository-preparation commit.

## 10. Binary hashes

Build outputs from `.pio/build/heltec-v4-r8-tft/`:

```text
65F2152CCEF0C02799553BE99EB8EB6AD39D83CF50903572A7DB65AE6F3CCFFC  firmware-heltec-v4-r8-tft-2.8.0.6989949.factory.bin
6F68B0EC578648B11CCF699AF3902D98F5E8DA4786107F70BA3762FD9C2E697C  firmware-heltec-v4-r8-tft-2.8.0.6989949.bin
057092CD73FF7C2020CF9D15E471054E3FE0BA24BD1C825BD600E49B0E64C2AB  littlefs-heltec-v4-r8-tft-2.8.0.6989949.bin
```

The LittleFS hash matches the known-good LittleFS image. Firmware image hashes differ because the build identifier/version changed from `7239fe8` to `6989949`.

## 11. Flash mode confirmation

Existing known-good `dist/` flash scripts still use:

`--flash-mode dio`

The new combined factory image generation also used:

```text
--flash_mode dio
```

## 12. LittleFS offset confirmation

Confirmed in current build manifest:

`0xc90000`

Confirmed in known-good `dist/heltec-v4-r8-tft/flash-map.json`:

```text
0x0
0xc90000
```

## 13. External known-good artifact archive path

`C:\Users\JPJYR\Documents\meshtastic-artifacts\known-good\2.8.0.7239fe8`

Archive files include:

- `ARCHIVE-README.md`
- `SHA256SUMS.txt`
- factory image
- app image
- LittleFS image
- bootloader/partition helper binaries
- flash scripts
- flash map
- manifest
- package ZIP
- flashing README

## 14. Archive SHA256 verification result

Every copied archive file was verified against its source file. All source/archive hash comparisons returned `OK`.

Key known-good hashes:

```text
138FDEB29C5D3D655113ACAE2522328583C8B625D09A8CC5A7FEC816AC7C0830  firmware-heltec-v4-r8-tft-2.8.0.7239fe8.factory.bin
057092CD73FF7C2020CF9D15E471054E3FE0BA24BD1C825BD600E49B0E64C2AB  littlefs-heltec-v4-r8-tft-2.8.0.7239fe8.bin
0BF3CAC43B0C5D14482B11FF199DEFD1706D99ADE22CECE5202C0579D2DBE655  firmware-heltec-v4-r8-tft-2.8.0.7239fe8.bin
FDCE35E146F7C69A413B29CCD124E26099EECEB6511A90172FB2D6130FC5E636  meshtastic-heltec-v4-r8-tft-esptool-package.zip
```

## 15. Current uncommitted changes

At report creation time, the expected uncommitted firmware changes are:

- `platformio.ini`: development-only Device UI dependency switch to `../device-ui`
- `docs/device-ui-development-workflow.md`: documentation of the exact local dependency switch
- `docs/quick-message-development-ready.md`: this readiness report

Expected untracked files intentionally not committed:

- `dist/`
- `docs/heltec-v4-r8-tft-programming-mode-investigation.md`
- `docs/heltec-v4-r8-tft-quick-message-ui-investigation.md`

Ignored local generated files:

- `.pio-core/`
- `build-*.log`

## 16. Exact rollback procedure

To return firmware dependency to the known-good upstream Device UI archive:

1. Restore `[device-ui_base]` in `platformio.ini` to:

```text
https://github.com/meshtastic/device-ui/archive/9d9b9df81fcde646811a10942d00d5f45f72af7b.zip
```

2. Keep flash mode as `dio`.
3. Keep LittleFS offset as `0xc90000`.
4. If hardware rollback is needed, use the archived package at:

```text
C:\Users\JPJYR\Documents\meshtastic-artifacts\known-good\2.8.0.7239fe8
```

5. Full clean flash map:

```text
0x0       firmware-heltec-v4-r8-tft-2.8.0.7239fe8.factory.bin
0xc90000  littlefs-heltec-v4-r8-tft-2.8.0.7239fe8.bin
```

## 17. Exact state before first Quick Message code modification

Firmware:

- Branch: `feature/heltec-r8-quick-messages`
- HEAD: `6989949e99d156e4be392a08602e0d9d7c35b3d9`
- Functional source changes: none
- Development dependency switch: local `../device-ui`, uncommitted

Device UI:

- Branch: `feature/heltec-r8-quick-messages`
- HEAD: `9d9b9df81fcde646811a10942d00d5f45f72af7b`
- Working tree: clean
- Functional source changes: none

Build:

- Target: `heltec-v4-r8-tft`
- Result: success
- Device UI source: `file://../device-ui`
- Flash mode: `dio`
- LittleFS offset: `0xc90000`

