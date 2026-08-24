# T-Deck and T5S3 Variant Polymorphism Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the T-Deck-Pro/MAX macro-heavy implementation and the T5S3 E-Paper V2 device branches with variant-owned C++ classes and virtual interfaces while preserving current hardware behavior for exactly three environments: `t-deck-pro-v1_1`, `t-deck-max`, and `t5s3-epaper-v2`.

**Architecture:** The common firmware will depend on small abstract services for lifecycle, board power, input, haptics, notification audio, and E-Ink UI policy. T-Deck-Pro V1.1, T-Deck-Max, and T5S3 E-Paper V2 will provide derived implementations from their own `variants/esp32s3/...` directories. PlatformIO `build_src_filter` will select those implementations; the existing default factory path remains unchanged for devices outside this migration. A preprocessor branch is allowed only in the factory if link-time variant selection cannot cover a specific platform edge case.

**Tech Stack:** C++17, PlatformIO, Arduino ESP32-S3, existing Meshtastic `Observer`, `OSThread`, `InputBroker`, `Screen`, and `MotionSensor` abstractions.

---

## Review Decision

The current PR1-PR5 series should not be extended. It adds approximately 6,677 lines across 67 files relative to `upstream/develop`, with T-Deck conditions spread through common input, graphics, power, audio, notification, sensor, and startup code. The replacement series will be based on `upstream/develop`, not on the rejected macro-heavy PR5 stack.

The current `firmware-pr6-check` worktree remains a reference copy of PR5. It is not the implementation branch for this refactor. A new refactor worktree will be created from `upstream/develop` after the architecture is accepted.

The T5S3 work already present in `firmware-pr6-check` is useful as a third variant reference, but it must not be folded into T-Deck-specific code. Its E-Paper coordinate mapping, partial-refresh policy, UI profile, and virtual keyboard are separate T5S3 implementations that consume the same common interfaces.

The response to the maintainer should be:

> I agree that the current macro-heavy series is not maintainable. I will stop extending PR1-PR5 and rework the feature around variant-owned C++ implementations. Common firmware will use small virtual interfaces, while T-Deck-Pro, T-Deck-Max, and T5S3 E-Paper V2 will provide derived classes from their variant directories. PlatformIO source filters will select those implementations, and any remaining preprocessor selection will be confined to a factory. I will submit this as small, independently buildable PRs and keep the root `platformio.ini` unchanged.

## Scope Boundary

This migration covers only these PlatformIO environments:

- `t-deck-pro-v1_1` (T-Deck Pro V1.1)
- `t-deck-max` (T-Deck MAX)
- `t5s3-epaper-v2` (T5S3 E-Paper V2)

No other device environment, display profile, InkHUD variant, control target, or radio target is an implementation or validation target for this plan. The existing root `platformio.ini` cache edit, the global NimBLE connection-parameter change, and its native test are separate worktree changes and must not be staged in these device PRs.

## Design Rules

1. Common files under `src/` must not include T-Deck-specific headers or test `T_DECK_PRO`, `T_DECK_MAX`, `_VARIANT_T_DECK_PRO_V1_1`, or `_VARIANT_T_DECK_MAX`. T5S3-specific selection must not be used to enter T-Deck code.
2. T-Deck hardware code belongs under `variants/esp32s3/t-deck-pro-v1_1/` or `variants/esp32s3/t-deck-max/`. T5S3 code belongs under `variants/esp32s3/t5s3_epaper/` or a T5S3-specific source directory. Shared T-Deck and T5S3 code belongs behind common interfaces, not in unrelated common modules.
3. A base class owns policy, event translation, scheduling, and lifetime. A derived class owns pins, I2C addresses, GPIO expanders, controller commands, and board-specific recovery.
4. Do not create one global `TDeckVariant` god class. Keep lifecycle, power, input, notification audio, haptic output, and UI policy as separate interfaces where their responsibilities differ.
5. Use typed `constexpr` values and enums for board data. Keep only build-system or Arduino-core definitions that must exist in `variant.h` or `platformio.ini`.
6. Preserve the root `platformio.ini`; dependencies and source filters for T-Deck hardware must live in the two T-Deck variant `.ini` files, and T5S3 settings must live in `variants/esp32s3/t5s3_epaper/platformio.ini`. The PR6 cache-only edits to the root file are not part of this migration.
7. Move existing behavior into the new classes before changing behavior. Each PR must have a narrow diff, a focused commit, and a compile check limited to the affected one or more of the three target environments.

## Target Interfaces

### `DeviceVariant`

Create `src/platform/DeviceVariant.h` and `src/platform/DeviceVariant.cpp` with a small lifecycle interface:

```cpp
class DeviceVariant {
  public:
    virtual ~DeviceVariant() = default;
    virtual void earlyInit() {}
    virtual void afterI2CInit() {}
    virtual void lateInit() {}
    virtual bool recoverI2C() { return false; }
    virtual void shutdown() {}
};

std::unique_ptr<DeviceVariant> createDeviceVariant();
```

The common implementation is a weak default factory. The T-Deck variant directories provide strong factories that return `TDeckProVariant` or `TDeckMaxVariant`. `src/main.cpp` owns the object and invokes the lifecycle methods at the existing `earlyInitVariant`, `initVariantAfterI2C`, `lateInitVariant`, recovery, and shutdown call sites.

### Hardware services

Create focused interfaces in `src/platform/` rather than adding methods to `DeviceVariant` for every peripheral:

- `DevicePowerController`: modem, LoRa, GPS, IMU, motor, antenna, audio route, safe-state, and I2C recovery operations.
- `DeviceInputProvider`: touch/keyboard source registration and raw-controller polling translated into `InputEvent`.
- `HapticOutput`: motor enable and effect output; the common haptic scheduler remains platform-independent.
- `NotificationAudio`: notification cue queue, volume, and shutdown; A7682E is a variant implementation.
- `DeviceUiPolicy`: E-Ink layout, refresh, banner, notification, and touch-target policy.

The exact interface headers are introduced only when the owning subsystem is migrated. Do not expose T-Deck types from these interfaces.

### T5S3-specific policy interfaces

T5S3 must use the same common interfaces without inheriting T-Deck hardware assumptions:

- `T5S3DisplayPolicy`: logical 540x960 coordinates, panel 960x540 mapping, framebuffer bit addressing, and partial/full refresh selection.
- `T5S3UiPolicy`: T5-specific font levels, card heights, footer reserve, navigation touch expansion, message layout, and refresh-region ownership.
- `T5S3Keyboard`: a C++ adapter around a pure-C `T5S3KeyboardCore` state machine. The core owns ASCII modes, cursor edits, length/character validation, key descriptors, submit/cancel results, and the bounded action queue; the adapter owns drawing, `TouchTargetRegistry` registration, callbacks, and page-generation handling.

T5S3 keeps its PCA9535/TPS651851/GT911/FastEPD hardware paths. The T5 UI profile is selected only by the `t5s3-epaper-v2` environment and is never inferred from a T-Deck macro.

## Task 1: Create the replacement branch and capture the baseline

**Files:**
- Create: `docs/superpowers/plans/2026-08-24-t-deck-variant-polymorphism-refactor.md`
- No firmware source changes in this task.

- [ ] **Step 1: Create a clean refactor worktree from upstream.**

```powershell
git fetch upstream
git worktree add -b refactor/t-deck-variant-polymorphism D:\Code2\Meshtastic\firmware-tdeck-variant-refactor upstream/develop
```

- [ ] **Step 2: Record the baseline build environments.**

```powershell
pio project config --json-output | Out-File baseline-platformio.json
git status --short --branch
```

Expected result: the new branch is clean and starts at `upstream/develop`. Do not copy the root `platformio.ini` change from the PR5 verification worktree.

- [ ] **Step 3: Record the macro inventory before migration.**

```powershell
rg -n "T_DECK_(PRO|MAX)|_VARIANT_T_DECK" src variants/esp32s3/t-deck-pro-v1_1 variants/esp32s3/t-deck-max
```

Save the output in the PR description so the final cleanup can prove that common-code references were removed.

- [ ] **Step 4: Commit only the plan if the repository workflow requires a tracked plan.**

```powershell
git add -- docs/superpowers/plans/2026-08-24-t-deck-variant-polymorphism-refactor.md
git commit -m "docs: plan T-Deck variant polymorphism refactor"
```

## Task 2: Add the lifecycle interface and variant source selection

**Files:**
- Create: `src/platform/DeviceVariant.h`
- Create: `src/platform/DeviceVariant.cpp`
- Create: `variants/esp32s3/t-deck-pro-v1_1/DeviceVariant.cpp`
- Create: `variants/esp32s3/t-deck-max/DeviceVariant.cpp`
- Modify: `src/main.cpp`
- Modify: `src/sleep.cpp`
- Modify: `variants/esp32s3/t-deck-pro-v1_1/platformio.ini`
- Modify: `variants/esp32s3/t-deck-max/platformio.ini`

- [ ] **Step 1: Add the default lifecycle class and weak factory.**

`src/platform/DeviceVariant.cpp` provides a no-op `DefaultDeviceVariant` and a weak `createDeviceVariant()` returning it. The factory must be valid for every existing Arduino target without including any T-Deck header.

- [ ] **Step 2: Add explicit variant source filters.**

Extend each T-Deck environment from its existing ESP32-S3 filter:

```ini
build_src_filter =
  ${esp32s3_base.build_src_filter}
  +<../variants/esp32s3/t-deck-pro-v1_1>
```

Use the corresponding `t-deck-max` path for MAX. Do not change the root `platformio.ini`.

- [ ] **Step 3: Move lifecycle behavior into derived classes.**

`TDeckProVariant` absorbs the current Pro `earlyInitVariant`, `lateInitVariant`, and I2C recovery logic. `TDeckMaxVariant` absorbs the current MAX lifecycle and safe-shutdown logic. Keep `variant.cpp` limited to Arduino board initialization that cannot be represented by the new service interfaces.

- [ ] **Step 4: Route common lifecycle calls through the object.**

Replace the direct T-Deck declarations and conditional calls in `src/main.cpp` and `src/sleep.cpp` with `deviceVariant->earlyInit()`, `afterI2CInit()`, `lateInit()`, `recoverI2C()`, and `shutdown()`. The common lifecycle must compile without any T-Deck macro.

- [ ] **Step 5: Verify the first migration.**

```powershell
pio run -e t-deck-pro-v1_1
pio run -e t-deck-max
```

Expected result: both affected T-Deck environments compile, and each T-Deck implementation is selected only by its own variant environment.

- [ ] **Step 6: Commit the isolated lifecycle change.**

```powershell
git add -- src/platform/DeviceVariant.h src/platform/DeviceVariant.cpp src/main.cpp src/sleep.cpp variants/esp32s3/t-deck-pro-v1_1 variants/esp32s3/t-deck-max
git commit -m "refactor: add polymorphic device variant lifecycle"
```

## Task 3: Move board power, GPIO expander, and sensor behavior behind services

**Files:**
- Create: `src/platform/DevicePowerController.h`
- Create: `src/platform/DeviceSensorProvider.h`
- Move: `src/platform/extra_variants/t_deck_max/TDeckMaxBoard.cpp` to `variants/esp32s3/t-deck-max/TDeckMaxBoard.cpp`
- Move: `src/platform/extra_variants/t_deck_max/TDeckMaxBoard.h` to `variants/esp32s3/t-deck-max/TDeckMaxBoard.h`
- Move: `src/platform/extra_variants/t_deck_max/TDeckMaxXL9555.cpp` to `variants/esp32s3/t-deck-max/TDeckMaxXL9555.cpp`
- Move: `src/platform/extra_variants/t_deck_max/TDeckMaxXL9555.hpp` to `variants/esp32s3/t-deck-max/TDeckMaxXL9555.hpp`
- Modify: `src/Power.cpp`
- Modify: `src/detect/ScanI2C.cpp`
- Modify: `src/detect/ScanI2CTwoWire.cpp`
- Modify: `src/gps/GPS.cpp`
- Modify: `src/motion/AccelerometerThread.h`
- Modify: `src/platform/esp32/architecture.h`
- Modify: `src/configuration.h`
- Modify: `src/main.cpp`

- [ ] **Step 1: Define the power service using board-neutral operations.**

The interface exposes `setModemPower`, `setLoRaPower`, `setGpsPower`, `setImuPower`, `setMotorPower`, `setAudioRoute`, `setAntenna`, `safeState`, and `recoverI2C`. It must not mention XL9555, T-Deck pins, or T-Deck macros.

- [ ] **Step 2: Implement Pro and MAX power controllers in their variant directories.**

The Pro implementation uses its direct GPIO and DRV2605 enable pin. The MAX implementation owns XL9555 register access, peripheral power sequencing, antenna preference, touch/keyboard reset, and safe-state handling. Preserve the current ordering and active levels exactly.

- [ ] **Step 3: Replace `Power.cpp` and `main.cpp` T-Deck branches with service calls.**

Remove the direct `T_DECK_MAX` and `_VARIANT_T_DECK_PRO_V1_1` branches from power-up, shutdown, motor, and I2C recovery paths. The default controller must preserve existing behavior for all other boards.

- [ ] **Step 4: Move board-specific sensor discovery into a provider.**

The MAX BHI260AP and LTR553ALS addresses, reset/IRQ behavior, charger address, and scan aliases are returned by `DeviceSensorProvider`. `AccelerometerThread` receives a `std::unique_ptr<MotionSensor>` from the provider/factory and never directly tests `T_DECK_MAX`.

- [ ] **Step 5: Remove common-header macro dependencies.**

Convert the new board data to typed constants in the variant headers. Keep Arduino pin aliases required by the board package, but do not use those aliases as feature-selection branches in common source.

- [ ] **Step 6: Verify board services.**

```powershell
pio run -e t-deck-pro-v1_1
pio run -e t-deck-max
```

Expected result: the three builds compile and the MAX BHI260AP path uses `unique_ptr` ownership through the provider.

- [ ] **Step 7: Commit the board-service migration.**

```powershell
git add -- src/Power.cpp src/detect/ScanI2C.cpp src/detect/ScanI2CTwoWire.cpp src/gps/GPS.cpp src/motion/AccelerometerThread.h src/platform/esp32/architecture.h src/configuration.h src/main.cpp src/platform/DevicePowerController.h src/platform/DeviceSensorProvider.h variants/esp32s3/t-deck-pro-v1_1 variants/esp32s3/t-deck-max
git commit -m "refactor: move T-Deck board services into variants"
```

## Task 4: Refactor touch and keyboard input into variant implementations

**Files:**
- Create: `src/input/DeviceInputProvider.h`
- Create: `variants/esp32s3/t-deck-pro-v1_1/TDeckProInput.cpp`
- Create: `variants/esp32s3/t-deck-max/TDeckMaxInput.cpp`
- Move: T-Deck controller code from `src/platform/extra_variants/t_deck_pro/variant.cpp` and `src/platform/extra_variants/t_deck_max/variant.cpp` into the two variant input classes
- Modify: `src/input/InputBroker.h`
- Modify: `src/input/TouchScreenBase.h`
- Modify: `src/input/TouchScreenBase.cpp`
- Modify: `src/input/TouchScreenImpl1.cpp`
- Modify: `src/input/TouchGestureRecognizer.cpp`
- Modify: `src/input/TouchTargetRegistry.cpp`
- Modify: `src/input/kbI2cBase.cpp`
- Modify: `src/input/TDeckProKeyboard.cpp`
- Modify: `src/input/TLoraPagerKeyboard.cpp`

- [ ] **Step 1: Define a board-neutral input provider.**

The provider creates/registers input sources and translates raw controller reports into the existing `InputEvent` structure. It exposes no CST328, CST3530, TCA8418, or XL9555 types to common code.

- [ ] **Step 2: Make `TouchScreenBase` a reusable gesture/input base class.**

Keep gesture recognition, target capture, long-press handling, coordinate transforms, and event publication in the base class. Replace board conditionals with virtual raw-input methods supplied by `DeviceInputProvider`.

- [ ] **Step 3: Implement Pro and MAX input providers.**

The Pro provider owns CST328/Pro touch recovery and TCA8418 keyboard setup. The MAX provider owns CST328/CST3530 detection, three capacitive key reports, XL9555 touch reset, and MAX keyboard reset. Preserve the current event IDs and source names.

- [ ] **Step 4: Keep unrelated input targets unchanged.**

The default provider must retain current behavior for existing touch and keyboard variants. Do not move generic `TouchGestureRecognizer` or `TouchTargetRegistry` into a T-Deck-only directory.

- [ ] **Step 5: Verify input compilation and event plumbing.**

```powershell
pio run -e t-deck-pro-v1_1
pio run -e t-deck-max
```

For hardware verification, confirm Pro touch tap/long-press and keyboard events, then confirm MAX capacitive keys and physical keyboard events. A missing event must be fixed in the provider, not by adding a new macro to `InputBroker`.

- [ ] **Step 6: Commit the input migration.**

```powershell
git add -- src/input variants/esp32s3/t-deck-pro-v1_1 variants/esp32s3/t-deck-max
git commit -m "refactor: provide T-Deck input through variant classes"
```

## Task 5: Refactor haptics, notification audio, and buzzer routing

**Files:**
- Create: `src/input/HapticOutput.h`
- Create: `src/audio/NotificationAudio.h`
- Create: `variants/esp32s3/t-deck-pro-v1_1/TDeckProHapticOutput.cpp`
- Create: `variants/esp32s3/t-deck-max/TDeckMaxHapticOutput.cpp`
- Move: `src/audio/A7682Audio.cpp` and `src/audio/A7682Audio.h` into the variant-owned notification-audio implementation, preserving the shared policy helpers
- Modify: `src/input/HapticFeedback.h`
- Modify: `src/input/HapticFeedback.cpp`
- Modify: `src/buzz/BuzzerFeedbackThread.cpp`
- Modify: `src/AudioThread.h`
- Modify: `src/modules/Modules.cpp`
- Modify: `src/modules/ExternalNotificationModule.cpp`
- Modify: `src/modules/ExternalNotificationModule.h`
- Modify: `src/mesh/MeshService.cpp`
- Modify: `src/graphics/draw/MenuHandler.cpp`
- Modify: `src/graphics/draw/MenuHandler.h`

- [ ] **Step 1: Separate common scheduling from hardware output.**

`HapticFeedback` retains effect mapping, enable state, persistence, delayed pulses, and throttling. `HapticOutput` becomes the virtual hardware boundary with `begin`, `setMotorPower`, and `writeEffect` operations. Pro and MAX provide derived outputs in their variant directories.

- [ ] **Step 2: Make notification audio an optional service.**

Common notification code calls `NotificationAudio::queueCue`, `setVolume`, and `shutdown`. The default implementation is unavailable/no-op; Pro and MAX register the A7682E implementation through their factories. `MenuHandler` consumes capabilities instead of including `A7682Audio.h` conditionally.

- [ ] **Step 3: Remove audio and haptic branches from common modules.**

`ExternalNotificationModule`, `MeshService`, `Modules`, `BuzzerFeedbackThread`, `AudioThread`, and `MenuHandler` must call the interfaces. The MAX ES8311/A7682E route remains inside `TDeckMaxVariant` and its audio controller.

- [ ] **Step 4: Verify hardware behavior.**

```powershell
pio run -e t-deck-pro-v1_1
pio run -e t-deck-max
```

On hardware, verify Pro touch/keyboard haptics, MAX touch/keyboard haptics, received-message haptics, and A7682E notification cues.

- [ ] **Step 5: Commit the notification-service migration.**

```powershell
git add -- src/input/HapticFeedback.h src/input/HapticFeedback.cpp src/input/HapticOutput.h src/audio src/AudioThread.h src/buzz/BuzzerFeedbackThread.cpp src/modules/Modules.cpp src/modules/ExternalNotificationModule.cpp src/modules/ExternalNotificationModule.h src/mesh/MeshService.cpp src/graphics/draw/MenuHandler.cpp src/graphics/draw/MenuHandler.h variants/esp32s3/t-deck-pro-v1_1 variants/esp32s3/t-deck-max
git commit -m "refactor: move T-Deck haptics and audio behind services"
```

## Task 6: Replace T-Deck UI conditionals with an E-Ink UI policy

**Files:**
- Create: `src/graphics/DeviceUiPolicy.h`
- Create: `src/graphics/DefaultEInkUiPolicy.cpp`
- Create: `variants/esp32s3/t-deck-pro-v1_1/TDeckProUiPolicy.cpp`
- Create: `variants/esp32s3/t-deck-max/TDeckMaxUiPolicy.cpp`
- Modify: `src/graphics/Screen.h`
- Modify: `src/graphics/Screen.cpp`
- Modify: `src/graphics/EInkDisplay2.h`
- Modify: `src/graphics/EInkDisplay2.cpp`
- Modify: `src/graphics/TouchLayout.h`
- Modify: `src/graphics/draw/UIRenderer.cpp`
- Modify: `src/graphics/draw/DebugRenderer.cpp`
- Modify: `src/graphics/draw/NodeListRenderer.cpp`
- Modify: `src/graphics/draw/NodeListRenderer.h`
- Modify: `src/graphics/draw/NotificationRenderer.cpp`
- Modify: `src/graphics/draw/NotificationRenderer.h`
- Modify: `src/graphics/draw/MessageRenderer.cpp`
- Modify: `src/graphics/draw/MenuHandler.cpp`
- Modify: `src/graphics/draw/MenuHandler.h`

- [ ] **Step 1: Define policy boundaries from existing behavior.**

The policy owns T-Deck layout constants, touch-target registration, E-Ink refresh decisions, banner placement, notification presentation, and device-specific menu capabilities. It does not own mesh state or duplicate renderer logic.

- [ ] **Step 2: Move existing T-Deck layout and notification blocks into derived policies.**

Move code rather than duplicate it. The default policy must retain the existing behavior for boards that do not opt into the T-Deck policy. Existing T5S3 policy behavior remains separate and is not silently changed.

- [ ] **Step 3: Make renderers call virtual policy methods.**

Remove T-Deck macro branches from the renderer files. A renderer asks the policy whether a target, banner, refresh, or menu item is supported and then executes the common rendering path.

- [ ] **Step 4: Verify UI behavior on both T-Deck devices.**

```powershell
pio run -e t-deck-pro-v1_1
pio run -e t-deck-max
```

On hardware, verify page navigation, touch targets, long press, message banners, node lists, refresh behavior, and keyboard navigation.

- [ ] **Step 5: Commit the UI policy migration.**

```powershell
git add -- src/graphics src/graphics/draw variants/esp32s3/t-deck-pro-v1_1 variants/esp32s3/t-deck-max
git commit -m "refactor: provide T-Deck E-Ink UI policies"
```

## Task 7: Migrate T5S3 E-Paper V2 display, refresh, and virtual keyboard

**Files:**
- Create: `src/platform/extra_variants/t5s3_epaper/T5S3KeyboardCore.h`
- Create: `src/platform/extra_variants/t5s3_epaper/T5S3KeyboardCore.c`
- Create: `src/platform/extra_variants/t5s3_epaper/T5S3Keyboard.h`
- Create: `src/platform/extra_variants/t5s3_epaper/T5S3Keyboard.cpp`
- Move during variant isolation: `src/graphics/T5S3EpaperRotation.h` to `variants/esp32s3/t5s3_epaper/T5S3EpaperRotation.h`
- Move during variant isolation: `src/graphics/T5S3EpaperUI.h` to `variants/esp32s3/t5s3_epaper/T5S3EpaperUI.h`
- Modify: `src/graphics/EInkParallelDisplay.cpp`
- Modify: `src/graphics/Screen.cpp`
- Modify: `src/graphics/SharedUIDisplay.cpp`
- Modify: `src/graphics/draw/UIRenderer.cpp`
- Modify: `src/graphics/draw/MessageRenderer.cpp`
- Modify: `src/graphics/draw/NodeListRenderer.cpp`
- Modify: `src/graphics/draw/NodeListRenderer.h`
- Modify: `src/graphics/draw/NotificationRenderer.cpp`
- Modify: `src/graphics/draw/NotificationRenderer.h`
- Modify: `src/graphics/draw/DebugRenderer.cpp`
- Modify: `src/graphics/VirtualKeyboard.cpp`
- Modify: `src/input/InputBroker.h`
- Modify: `src/input/TouchGestureRecognizer.cpp`
- Modify: `src/input/TouchScreenBase.cpp`
- Modify: `src/input/TouchScreenBase.h`
- Modify: `src/input/TouchScreenImpl1.cpp`
- Modify: `src/input/TouchTargetRegistry.cpp`
- Modify: `src/modules/OnScreenKeyboardModule.h`
- Modify: `src/modules/OnScreenKeyboardModule.cpp`
- Modify: `src/modules/CannedMessageModule.cpp`
- Modify: `src/platform/extra_variants/t5s3_epaper/variant.cpp`
- Modify: `variants/esp32s3/t5s3_epaper/platformio.ini`
- Create: `test/test_t5s3_keyboard_core/test_main.cpp`
- Create: `test/test_t5s3_epaper_rotation/test_main.cpp`

- [ ] **Step 1: Move T5 coordinate conversion behind the T5 display policy.**

Keep the logical UI size at 540x960 and the physical parallel-panel size at 960x540. Use one mapping in both directions:

```text
logical -> panel: x = logical.y, y = 539 - logical.x
GT911 raw -> logical: x = 539 - raw.y, y = raw.x
```

`EInkParallelDisplay` owns framebuffer bit addressing and physical row ranges. Common gesture code receives logical coordinates and must not rotate them a second time. `flip_screen` remains a generic touch-layer option and must not be applied again by the T5 UI.

- [ ] **Step 2: Move T5 V2 refresh policy out of the shared base configuration.**

Keep the existing `-D FAST_EPD_PARTIAL_UPDATE_BUG` in `[t5s3_epaper_base]` so no other T5 configuration changes. Under `[env:t5s3-epaper-v2]`, add `build_unflags = -D FAST_EPD_PARTIAL_UPDATE_BUG`; this removes the compatibility flag only for V2. V2 normal page transitions and state changes use `partialUpdate(startRow, endRow)`, while boot, periodic ghosting maintenance, severe ghosting recovery, and firmware-update pages use the explicit full-update path.

Do not alter the PCA9535/TPS651851 initialization order or treat the electronic-paper power-control signals as ordinary ESP32 GPIOs.

- [ ] **Step 3: Isolate the T5 UI profile.**

Move the T5-specific font and layout values into `T5S3UiPolicy`: 540x960 logical bounds, 40 px navigation icons, 120 px footer reserve, 16 px navigation touch expansion, 52-56 px minimum touch targets, approximately 76-88 px node cards, and 58-64 px virtual-keyboard keys. The profile may select font levels and dimensions but must not change global `ScreenFonts` defaults or T-Deck hardware code.

- [ ] **Step 4: Implement the pure-C keyboard state machine.**

`T5S3KeyboardCore.c` and `.h` contain no Arduino, `OLEDDisplay`, `Screen`, `std::string`, `std::function`, or dynamic allocation. It owns:

- lower, upper, special, and number modes;
- stable key IDs, labels, row and width weights;
- character filtering, insertion, cursor movement, backspace, delete, and maximum length;
- one-shot Shift, mode changes, submit/cancel results, and empty-submit rejection;
- a bounded FIFO action queue that never blocks the touch/input path.

The native test must cover insertion, cursor editing, Shift, all modes, filtering, maximum length, submit/cancel, stable key descriptors, and queue ordering/full behavior.

- [ ] **Step 5: Add the C++ T5 keyboard adapter.**

`T5S3Keyboard.cpp` owns the 540x960 drawing, text cursor, pressed-key feedback, `TouchTargetRegistry` targets, page-generation invalidation, callback lifetime, timeout, and partial-refresh rectangles. It translates submit/cancel into the existing `OnScreenKeyboardModule` callback contract. A touch target that is pressed and then left is consumed without triggering page navigation; only release inside the target activates the key.

- [ ] **Step 6: Integrate the shared text-input session.**

Under the T5 V2 variant selection, `OnScreenKeyboardModule` owns one T5 keyboard session. `NotificationRenderer` delegates the text-input overlay to the T5 adapter and suppresses background navigation while it is active. `CannedMessageModule` uses the same session and preserves send, save/complete, cancel, timeout, and empty-text semantics. Non-T5 boards keep the existing `VirtualKeyboard` and canned-message paths.

- [ ] **Step 7: Keep the T5S3 V2 boundary explicit.**

Compile the T5S3 display policy, UI profile, and keyboard adapter only from the `t5s3-epaper-v2` environment's source filter. Do not add the V2 definitions to a shared base environment or to any other variant configuration.

- [ ] **Step 8: Enable the T5 features only in the V2 environment.**

Keep these definitions in `variants/esp32s3/t5s3_epaper/platformio.ini` under `[env:t5s3-epaper-v2]`:

```ini
-D T5_S3_EPAPER_PRO_V2
-D MESHTASTIC_T5S3_EPAPER_V2_UI
-D T5S3_EPD_TOUCH_KEYBOARD
-D SDCARD_USE_SPI1
-D GPS_POWER_TOGGLE
```

The source filter must select the T5 variant directory explicitly. No T5 feature definition may be added to the root `platformio.ini` or another board's `.ini` file.

- [ ] **Step 9: Run the focused native checks and T5 build.**

```powershell
pio test -e native -f test_t5s3_keyboard_core
pio test -e native -f test_t5s3_epaper_rotation
pio run -e t5s3-epaper-v2
```

Expected result: both native suites pass; V2 compiles without `FAST_EPD_PARTIAL_UPDATE_BUG` and uses the partial-refresh policy.

- [ ] **Step 10: Commit the isolated T5S3 migration.**

```powershell
git add -- src/graphics/EInkParallelDisplay.cpp src/graphics/Screen.cpp src/graphics/SharedUIDisplay.cpp src/graphics/draw src/graphics/VirtualKeyboard.cpp src/input src/modules/OnScreenKeyboardModule.h src/modules/OnScreenKeyboardModule.cpp src/modules/CannedMessageModule.cpp src/platform/extra_variants/t5s3_epaper variants/esp32s3/t5s3_epaper test/test_t5s3_keyboard_core test/test_t5s3_epaper_rotation
git commit -m "refactor: isolate T5S3 E-Paper V2 UI and keyboard"
```

## Task 8: Remove obsolete variant glue and prove scope isolation

**Files:**
- Delete after migration: `src/platform/extra_variants/t_deck_pro/variant.cpp`
- Delete after migration: `src/platform/extra_variants/t_deck_max/variant.cpp`
- Delete after migration: `src/platform/extra_variants/t_deck_max/TDeckMaxBoard.cpp`
- Delete after migration: `src/platform/extra_variants/t_deck_max/TDeckMaxBoard.h`
- Delete after migration: `src/platform/extra_variants/t_deck_max/TDeckMaxXL9555.cpp`
- Delete after migration: `src/platform/extra_variants/t_deck_max/TDeckMaxXL9555.hpp`
- Modify: `src/AudioThread.h`
- Modify: `src/configuration.h`
- Modify: `src/main.cpp`
- Modify: `src/Power.cpp`
- Modify: `src/gps/GPS.cpp`
- Modify: `src/detect/ScanI2C.cpp`
- Modify: `src/detect/ScanI2CTwoWire.cpp`
- Modify: `src/sleep.cpp`
- Modify: `src/mesh/NodeDB.cpp`
- Modify: `src/modules/Telemetry/EnvironmentTelemetry.cpp`
- Delete after T5 migration: `src/platform/extra_variants/t5s3_epaper/variant.cpp`
- Delete after T5 migration: `src/platform/extra_variants/t5s3_epaper/T5S3KeyboardCore.h`
- Delete after T5 migration: `src/platform/extra_variants/t5s3_epaper/T5S3KeyboardCore.c`
- Delete after T5 migration: `src/platform/extra_variants/t5s3_epaper/T5S3Keyboard.h`
- Delete after T5 migration: `src/platform/extra_variants/t5s3_epaper/T5S3Keyboard.cpp`
- Delete after T5 migration: `src/graphics/T5S3EpaperRotation.h`
- Delete after T5 migration: `src/graphics/T5S3EpaperUI.h`

- [ ] **Step 1: Remove old include paths and dead weak hooks.**

Every caller must use the new factory/service object. Do not leave compatibility wrappers that reintroduce the old global functions or macro branches.

- [ ] **Step 2: Restrict build metadata to variant files.**

Keep T-Deck-specific dependencies in `variants/esp32s3/t-deck-max/platformio.ini` and the Pro variant `.ini` only. Keep the T5S3 V2 refresh override and keyboard/UI definitions in `variants/esp32s3/t5s3_epaper/platformio.ini`. Do not change the root `platformio.ini` or any non-target variant configuration. The `build_cache_dir`/`build_cache` edit observed in PR6 is local build infrastructure and must be kept out of this migration.

- [ ] **Step 3: Run the macro-scope audit.**

```powershell
rg -n "T_DECK_(PRO|MAX)|_VARIANT_T_DECK|T5_S3_EPAPER_PRO|MESHTASTIC_T5S3_EPAPER_V2_UI|T5S3_EPD_TOUCH_KEYBOARD" src --glob '*.{h,hpp,c,cc,cpp}'
```

Expected result: no T-Deck or T5S3 device-selection references in common `src/` files. Any remaining match must be in the single documented factory or be removed before review. Variant directories and their `.ini` files are the only allowed selection boundary.

- [ ] **Step 4: Run the final build matrix.**

```powershell
pio run -e t-deck-pro-v1_1
pio run -e t-deck-max
pio run -e t5s3-epaper-v2
```

These are the only three build environments in scope: the two T-Deck variants and T5S3 E-Paper V2. No build or validation command for another device belongs in this migration.

- [ ] **Step 5: Review the final diff and commit cleanup.**

```powershell
git diff --stat upstream/develop...HEAD
git diff --check
git status --short --branch
git add -- src variants/esp32s3/t-deck-pro-v1_1 variants/esp32s3/t-deck-max variants/esp32s3/t5s3_epaper test/test_t5s3_keyboard_core test/test_t5s3_epaper_rotation
git commit -m "refactor: remove device-specific macro integration glue"
```

## PR Sequence

The replacement should be submitted as six small PRs, each based on the previous refactor PR:

1. `refactor/t-deck-variant-lifecycle`: interfaces, weak default factory, explicit variant source filters, and lifecycle migration.
2. `refactor/t-deck-variant-hardware`: power, GPIO expander, I2C recovery, sensor discovery, and motion-sensor construction.
3. `refactor/t-deck-variant-input`: Pro/MAX touch and keyboard providers with no UI or audio changes.
4. `refactor/t-deck-variant-notifications`: haptic output, A7682E notification audio, and buzzer routing.
5. `refactor/device-eink-ui-policy`: T-Deck UI policy and the common E-Ink policy boundary.
6. `refactor/t5s3-epaper-v2-ui`: T5S3 rotation, partial refresh, UI profile, virtual keyboard, focused native tests, and final scope cleanup.

The existing NimBLE change in `src/nimble/NimbleBluetooth.cpp` and `test/test_nimble_connection_params/test_main.cpp` is a separate global connection-parameter safety fix. It is outside this three-device migration and must not be staged or described as part of these PRs. The root `platformio.ini` cache-only edit is local tooling state and must not be committed with this series.

No PR should include unrelated generated files, the root `platformio.ini`, or a second feature while the architecture is being migrated. Each PR description must state its base branch, the exact device environments built, and which common-code macro references were removed.

## Completion Criteria

- T-Deck-Pro V1.1 and T-Deck-Max behavior is provided by derived classes selected from their own variant directories.
- T5S3 E-Paper V2 rotation, refresh policy, UI profile, and virtual keyboard are provided by T5S3-specific variant classes/adapters.
- Common `src/` code no longer contains scattered T-Deck preprocessor branches.
- Common `src/` code no longer uses T5S3 macros to select UI, keyboard, rotation, or refresh behavior.
- Any remaining preprocessor selection is confined to a documented factory or required build metadata.
- The root `platformio.ini` and every non-target variant configuration are unchanged.
- `t-deck-pro-v1_1`, `t-deck-max`, and `t5s3-epaper-v2` compile at the relevant PR checkpoints.
- T5S3 native keyboard-core and coordinate/rotation tests pass.
- Touch, keyboard, haptic, notification audio, power sequencing, sensor discovery, E-Ink refresh, and shutdown behavior are preserved on the target devices.
- The final diff is materially smaller and easier to review than the current 6,677-line macro-heavy series.
