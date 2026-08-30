# Device UI Development Workflow

## Where Device UI Is Edited

Do not edit:

`.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/`

That is PlatformIO-generated dependency state.

Edit Device UI in the sibling repository:

`C:\Users\JPJYR\Documents\device-ui`

Current prepared branch:

`feature/heltec-r8-quick-messages`

Current baseline commit:

`9d9b9df81fcde646811a10942d00d5f45f72af7b`

## How Firmware Consumes Device UI

Firmware currently consumes Device UI from an exact archive URL in root `platformio.ini`:

```ini
[device-ui_base]
lib_deps =
	https://github.com/meshtastic/device-ui/archive/9d9b9df81fcde646811a10942d00d5f45f72af7b.zip
```

The `heltec-v4-r8-tft` environment inherits this through `variants/esp32s3/heltec_v4_r8/platformio.ini`.

## Local Development Dependency

For local work, use a dedicated firmware feature branch and temporarily replace the Device UI dependency with a local path dependency.

PlatformIO supports local library dependencies by path. From this firmware repository, the sibling checkout can be referenced as:

```ini
[device-ui_base]
lib_deps =
	# DEVELOPMENT ONLY for feature/heltec-r8-quick-messages:
	# use the editable local checkout at C:\Users\JPJYR\Documents\device-ui.
	# Final reproducible builds must switch back to an exact pinned commit archive.
	../device-ui
```

This should be treated as a development-only dependency. Do not use this as the final production/release dependency reference.

Current firmware feature branch dependency switch:

- Branch: `feature/heltec-r8-quick-messages`
- Local path dependency: `../device-ui`
- Absolute local source path: `C:\Users\JPJYR\Documents\device-ui`
- Device UI baseline at switch time: `9d9b9df81fcde646811a10942d00d5f45f72af7b`

## Final Reproducible Dependency

After testing Device UI changes:

1. Commit the Device UI change in `C:\Users\JPJYR\Documents\device-ui` only after user approval.
2. Push it only after user approval.
3. Point firmware to an exact commit archive from the fork or accepted upstream commit.
4. Avoid `master`, `main`, `develop`, or other moving branch names as final dependency references.

Example final style:

```ini
[device-ui_base]
lib_deps =
	https://github.com/<fork-or-upstream>/device-ui/archive/<exact-commit>.zip
```

## Ranked Development Options

1. Separate local clone with local path dependency during development, then exact commit archive for final builds. Best balance for Codex work, diff review, reproducibility, rebasing, and rollback.
2. GitHub fork pinned to exact commit. Best for shared reproducible builds after local testing.
3. Exact zip/archive pinned to commit. Good final dependency mechanism and already used by this firmware checkout.
4. Git submodule. Technically possible, but more intrusive for this firmware repo and unnecessary unless the team wants submodule management.
5. Permanent unversioned local dependency. Convenient, but poor reproducibility; use only temporarily on a development branch.

## Generated UI Source Ownership

The generated files are committed to Device UI, but they are not the authoring source.

Relevant generated outputs:

- `generated/ui_320x240/screens.c`
- `generated/ui_320x240/screens.h`

Authoring files found:

- `studio/320x240/TFT320x240.eez-project`
- `studio/240x320/TFTView_240x320.eez-project`

The generated directory README says:

```text
This directory contains the exported ui files. Use eez-studio to design the UI and generate the C code.
```

`CMakeLists.txt` includes generated sources from `generated/${GENERATED_VIEW}`.

Practical rule: for UI layout work, identify and update the EEZ Studio project first. Manual edits to generated `screens.c` and `screens.h` are fragile and may be overwritten by regeneration.

## Returning to Upstream Device UI

To return to the current upstream pinned Device UI:

1. Restore `[device-ui_base]` in firmware `platformio.ini` to:

```text
https://github.com/meshtastic/device-ui/archive/9d9b9df81fcde646811a10942d00d5f45f72af7b.zip
```

2. Remove any local dependency override from the firmware feature branch.
3. Rebuild `heltec-v4-r8-tft`.
4. Confirm flash scripts still use `--flash-mode dio`.

## Avoiding `.pio/libdeps` Edits

Use `.pio/libdeps` only for inspection.

Before changing Device UI code, confirm:

```powershell
git -C ..\device-ui status
git -C ..\device-ui branch --show-current
git -C ..\device-ui rev-parse HEAD
```

Then modify files under `..\device-ui`, not under `.pio\libdeps`.
