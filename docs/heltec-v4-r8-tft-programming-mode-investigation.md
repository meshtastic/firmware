# Heltec V4 R8 TFT Programming Mode Investigation

## 1. Executive summary

`heltec-v4-r8-tft` does not enter Programming Mode because the TFT, touch, or Bluetooth hardware is broken. It enters Programming Mode because this target builds the Device UI through `USE_PACKET_API`, and `PacketAPI::runOnce()` intentionally treats `config.bluetooth.enabled == true` as Programming Mode on non-Portduino targets.

The decisive source path is:

1. `variants/esp32s3/heltec_v4_r8/platformio.ini` enables `HAS_TFT`, `HAS_SCREEN`, and `USE_PACKET_API`.
2. `src/main.cpp` starts `tftSetup()` when the display mode is color.
3. `src/graphics/tftSetup.cpp` wires Device UI to firmware through `PacketAPI`.
4. `src/mesh/api/PacketAPI.cpp` stops sending normal `FromRadio` data to the Device UI when Bluetooth is enabled and instead sends a one-time Bluetooth config packet.
5. `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/source/graphics/TFT/TFTView_320x240.cpp` interprets that Bluetooth config during early boot as Programming Mode.

Wi-Fi did not exit Programming Mode because the Programming Mode decision is keyed on `config.bluetooth.enabled`, not on `config.network.wifi_enabled`. Enabling Wi-Fi may stop BLE from starting on ESP32, but it does not clear the persisted Bluetooth config bit that drives `PacketAPI`.

BLE and MUI appear technically capable of existing in the same firmware image: the target includes both Bluetooth-capable board metadata and the TFT/LVGL stack, and there is no source-level pin or bus conflict between BLE and the TFT/touch configuration. The current limitation is a deliberate software policy in the Packet API / Device UI bridge, not an obvious hardware conflict.

## 2. Exact source files involved

- `variants/esp32s3/heltec_v4_r8/platformio.ini`
  - Target and compile-time flags for `env:heltec-v4-r8-tft`.
- `boards/heltec_v4_r8.json`
  - ESP32-S3 board definition; declares `wifi`, `bluetooth`, and `lora` connectivity.
- `variants/esp32s3/heltec_v4_r8/variant.h`
  - Heltec V4 R8 pins and TFT enable macros.
- `src/main.cpp`
  - Loads config, releases unused BT memory, starts TFT setup, and later starts Wi-Fi.
- `src/platform/esp32/main-esp32.cpp`
  - ESP32 Bluetooth/Wi-Fi coexistence and Bluetooth memory release logic.
- `src/graphics/tftSetup.cpp`
  - Creates Device UI, `PacketAPI`, `PacketServer`, and `PacketClient`.
- `src/mesh/api/PacketAPI.cpp`
  - Core Programming Mode trigger.
- `src/mesh/NodeDB.cpp`
  - Default config and persistent config saving.
- `src/modules/AdminModule.cpp`
  - Applies Bluetooth and Wi-Fi config writes, saves them, and schedules reboot.
- `src/mesh/wifi/WiFiAPClient.cpp`
  - Starts Wi-Fi if configured, but does not alter Bluetooth config.
- `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/README.md`
  - Documents the intended Programming Mode behavior.
- `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/include/graphics/common/MeshtasticView.h`
  - Defines Device UI state machine states including Programming Mode.
- `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/source/graphics/common/ViewController.cpp`
  - Requests config, receives packets, and routes Bluetooth config to the view.
- `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/source/graphics/TFT/TFTView_320x240.cpp`
  - Implements Programming Mode entry and exit UI.

## 3. Exact functions involved

- `main()` in `src/main.cpp`
  - Lines 867-873: constructs `NodeDB`, loading persisted config before Bluetooth init.
  - Lines 878-880: calls `tftSetup()` when `HAS_TFT` and color display mode are active.
  - Lines 1200-1202: calls `initWifi()` later in boot.
- `tftSetup()` in `src/graphics/tftSetup.cpp`
  - Lines 331-334: creates `DeviceScreen`, `PacketAPI`, `PacketServer`, and initializes Device UI with `PacketClient`.
- `tft_task_handler()` in `src/graphics/tftSetup.cpp`
  - Lines 314-320: repeatedly runs `deviceScreen->task_handler()` and sleep handling.
- `PacketAPI::runOnce()` in `src/mesh/api/PacketAPI.cpp`
  - Lines 35-52: if `config.bluetooth.enabled` is true, enters `programmingMode` and does not call `sendPacket()`.
- `PacketAPI::notifyProgrammingMode()` in `src/mesh/api/PacketAPI.cpp`
  - Lines 120-129: sends only Bluetooth config to force client-side Programming Mode.
- `PacketAPI::receivePacket()` in `src/mesh/api/PacketAPI.cpp`
  - Lines 54-99: still accepts `ToRadio` packets, including config requests, but the normal outbound stream remains blocked while Bluetooth is enabled.
- `NodeDB::installDefaultConfig()` in `src/mesh/NodeDB.cpp`
  - Lines 1108-1117: TFT targets including `HELTEC_V4_R8_TFT` default Bluetooth to off.
  - Lines 1143-1148: screen devices default to random PIN unless overridden.
- `NodeDB::saveToDisk()` path in `src/mesh/NodeDB.cpp`
  - Lines 3252-3262: marks config sections present and saves `config` to `configFileName`.
- `AdminModule::handleSetConfig()` in `src/modules/AdminModule.cpp`
  - Lines 868-873: config writes default to `requiresReboot = true`.
  - Lines 964-971: accepts Wi-Fi/network config.
  - Bluetooth case is in the same switch below the inspected block and follows the same save/reboot pattern.
- `AdminModule::saveChanges()` in `src/modules/AdminModule.cpp`
  - Lines 1877-1891: persists changes and reboots when requested.
- `initWifi()` in `src/mesh/wifi/WiFiAPClient.cpp`
  - Lines 364-428: starts Wi-Fi only when `wifi_enabled` and SSID are set.
- `TFTView_320x240::init()` in Device UI
  - Lines 160-197: initializes boot screen and installs the Programming Mode timer.
- `TFTView_320x240::setupUIConfig()` in Device UI
  - Lines 202-228: if already entering/programming/reboot state, calls `enterProgrammingMode()` instead of completing normal UI setup.
- `TFTView_320x240::enterProgrammingMode()` in Device UI
  - Lines 518-548: either enables Bluetooth and waits for reboot, or shows the Programming Mode screen.
- `TFTView_320x240::timer_event_programming_mode()` in Device UI
  - Lines 959-969: boot-logo long-hold path into Programming Mode.
- `TFTView_320x240::ui_event_LogoButton()` in Device UI
  - Lines 971-1004: click/long-press behavior on boot logo.
- `TFTView_320x240::ui_event_BluetoothButton()` in Device UI
  - Lines 1006-1019: long-press Bluetooth button disables Bluetooth and sends Bluetooth config.
- `TFTView_320x240::updateBluetoothConfig()` in Device UI
  - Lines 6351-6363: receiving Bluetooth config during early boot calls `enterProgrammingMode()`.
- `ViewController::runOnce()` in Device UI
  - Lines 54-66: requests config and avoids receiving normal data when already in Programming Mode.
- `ViewController::handleFromRadio()` in Device UI
  - Lines 686-700: drops packets while in Programming Mode, but allows Bluetooth config before setup is complete.
  - Lines 763-765: passes Bluetooth config to `updateBluetoothConfig()`.

## 4. Exact compile-time flags involved

From `variants/esp32s3/heltec_v4_r8/platformio.ini`:

- Lines 36-48 define `env:heltec-v4-r8-tft` and extend `heltec_v4_r8_base`.
- Lines 50-52 define `HELTEC_V4_R8_TFT`.
- Lines 68-69 define `HAS_SCREEN=1` and `HAS_TFT=1`.
- Line 82 defines `USE_PACKET_API`.
- Lines 83-87 select the LovyanGFX driver and `VIEW_240x320`.
- Lines 89-95 define TFT SPI/backlight/reset pins.
- Lines 96-100 define custom touch driver and touch pins.
- Lines 111-115 define `ST7789_SPI_HOST=SPI3_HOST`, `SPI_FREQUENCY=75000000`, and SPI read settings.
- Later target flags define SD card on the same SPI pins with separate CS.

From `variants/esp32s3/heltec_v4_r8/variant.h`:

- Lines 55-58: `#if HAS_TFT`, then `HAS_SPI_TFT=1` and `USE_TFTDISPLAY=1`.

From `boards/heltec_v4_r8.json`:

- Lines 21-24: MCU is `esp32s3`; connectivity includes `wifi`, `bluetooth`, and `lora`.
- Lines 16-19: 240 MHz CPU, 80 MHz flash, `qio` board metadata, OPI PSRAM.

## 5. Boot-time decision flow

1. `src/main.cpp` constructs `NodeDB` at lines 867-869. That loads persisted config from flash.
2. Immediately afterward, `esp32ReleaseBluetoothMemoryIfUnused()` is called at lines 870-873. This uses the saved Bluetooth/Wi-Fi configuration before Bluetooth is initialized.
3. If `HAS_TFT` and display mode is color, `main.cpp` calls `tftSetup()` at lines 878-880.
4. `tftSetup()` creates the Device UI and Packet API bridge at `src/graphics/tftSetup.cpp` lines 331-334.
5. Device UI `TFTView_320x240::init()` creates the boot UI and a 3-second Programming Mode timer at Device UI lines 160-197.
6. `ViewController::runOnce()` requests firmware config when the UI reaches `eBootScreenDone` or `eEnterProgrammingMode`, lines 54-59.
7. `PacketAPI::runOnce()` checks `config.bluetooth.enabled` at `src/mesh/api/PacketAPI.cpp` line 39.
8. If Bluetooth is enabled, `PacketAPI` sets its internal `programmingMode` flag and calls `notifyProgrammingMode()` instead of `sendPacket()`, lines 40-48.
9. `notifyProgrammingMode()` sends only Bluetooth config, lines 120-129.
10. Device UI receives that Bluetooth config through `ViewController::handleFromRadio()` lines 699-765.
11. `TFTView_320x240::updateBluetoothConfig()` copies the config and, if still early in boot, calls `enterProgrammingMode()`, lines 6351-6363.
12. `enterProgrammingMode()` shows `">> Programming mode <<"` and the fixed PIN, lines 537-546.

Clean flash behavior is different: `NodeDB.cpp` lines 1108-1117 defaults Bluetooth off for TFT targets including `HELTEC_V4_R8_TFT`, so the normal UI can boot until some later config write enables Bluetooth.

## 6. Runtime decision flow

While running, the important loop is:

1. Device UI runs on its own task from `tft_task_handler()` in `src/graphics/tftSetup.cpp` lines 314-320.
2. The Device UI talks to the firmware through `PacketClient` / `PacketServer` created in `tftSetup()`.
3. `PacketAPI::runOnce()` is the firmware side of that channel.
4. If `config.bluetooth.enabled == false`, `PacketAPI::runOnce()` calls `sendPacket()` at line 48 and normal `FromRadio` traffic reaches MUI.
5. If `config.bluetooth.enabled == true`, `PacketAPI::runOnce()` sends only one Programming Mode notification and then suppresses normal outbound packets.
6. Device UI still can send config writes back through `PacketAPI::receivePacket()`, lines 54-99.
7. Once Device UI state is `eProgrammingMode`, `ViewController::handleFromRadio()` returns immediately at lines 686-689 and ignores normal radio updates.

This explains the observed "stuck" feeling: after Bluetooth is enabled, both sides have state-machine logic that keeps the UI in the Programming Mode path.

## 7. Why Wi-Fi enable did not exit Programming Mode

Wi-Fi enable did not exit Programming Mode because no inspected Programming Mode branch uses `config.network.wifi_enabled` as the condition to leave Programming Mode.

The controlling firmware condition is only:

- `src/mesh/api/PacketAPI.cpp` line 39: `if (config.bluetooth.enabled)`.

Wi-Fi appears elsewhere:

- `src/mesh/wifi/WiFiAPClient.cpp` lines 364-428 starts Wi-Fi when `config.network.wifi_enabled` and SSID are configured.
- `src/platform/esp32/main-esp32.cpp` lines 76-81 may release Bluetooth memory when Wi-Fi is configured to disable Bluetooth for that boot.

That memory/resource decision is not the same as clearing `config.bluetooth.enabled`. If the persisted Bluetooth config remains true, `PacketAPI` continues to choose Programming Mode even when Wi-Fi is enabled or BLE service is not started.

## 8. Whether BLE + MUI are technically capable of coexisting

Technically, likely yes at the firmware-image and hardware level:

- The target board declares Bluetooth support in `boards/heltec_v4_r8.json` lines 21-24.
- The same target enables TFT/MUI through `HAS_SCREEN=1`, `HAS_TFT=1`, and `USE_PACKET_API` in `variants/esp32s3/heltec_v4_r8/platformio.ini` lines 68-82.
- TFT and touch pins are SPI/I2C GPIO definitions in `platformio.ini` lines 89-115 and do not overlap with BLE, which is internal ESP32-S3 radio/NimBLE functionality.
- Boot logs from the successfully flashed DIO build showed the TFT UI and SD card initialized when Bluetooth was not enabled.

But in the current source, BLE + normal MUI are not allowed to coexist by policy:

- `PacketAPI::runOnce()` suppresses normal Device UI outbound traffic whenever `config.bluetooth.enabled` is true.
- Device UI README lines 179-180 explicitly lists disabling the screen to allow USB serial/BT and allowing Bluetooth through "Programming Mode".

So the practical answer is: the hardware/RTOS build can contain both, but the current Packet API bridge intentionally switches the screen into Programming Mode when Bluetooth is enabled.

## 9. Current workaround without reflashing

Disable Bluetooth in persisted config, then reboot.

Available source-backed ways:

- On the Programming Mode screen, long-press the Bluetooth button. `TFTView_320x240::ui_event_BluetoothButton()` lines 1006-1019 sets `bluetooth.enabled = false` and sends Bluetooth config back to firmware.
- From a connected client, write Bluetooth config with `enabled=false`.
- After the config write, let the device reboot. `AdminModule::handleSetConfig()` defaults config changes to requiring reboot at lines 868-873, and `AdminModule::saveChanges()` persists and schedules reboot at lines 1877-1891.

If it does not visibly reboot after using the on-screen Bluetooth button, wait a few seconds for the config write to finish and press reset/power-cycle once. On next boot, `config.bluetooth.enabled` should be false and `PacketAPI::runOnce()` should resume the normal `sendPacket()` path.

No reflashing is required unless the config partition is corrupt or the UI cannot send the config write.

## 10. Proposed fixes ranked

### 1. Minimal fix

Change `PacketAPI::runOnce()` so Bluetooth-enabled state alone does not force Programming Mode for Device UI on this target.

Possible narrow condition:

- Only enter Programming Mode when an explicit programming-mode flag/request is active.
- Or exempt selected TFT targets such as `HELTEC_V4_R8_TFT` from the `config.bluetooth.enabled` gate.

This is the smallest behavioral change but risks missing the original reason why Device UI disabled itself during BT/USB usage.

### 2. Clean architectural fix

Separate "Bluetooth is enabled" from "Device UI is in Programming Mode".

That means adding an explicit state or API signal, for example:

- `PacketAPI::programmingModeRequested`
- Device UI command/request to enter Programming Mode
- Config or runtime-only flag distinct from `config.bluetooth.enabled`

Then `PacketAPI::runOnce()` would suppress normal MUI traffic only when Programming Mode is explicitly requested, not whenever BLE is enabled.

### 3. Experimental fix

Allow normal `sendPacket()` even when Bluetooth is enabled, and make Device UI ignore only conflicting operations during active app pairing/config sessions.

This would test true BLE + MUI coexistence, but it has the highest risk of exposing concurrency, heap, or API arbitration problems.

## 11. Recommended first patch

Recommended first patch: replace the broad Bluetooth gate in `src/mesh/api/PacketAPI.cpp` with an explicit Programming Mode trigger.

Current behavior at lines 39-48:

```cpp
if (config.bluetooth.enabled) {
    if (!programmingMode) {
        programmingMode = true;
        success = notifyProgrammingMode();
    }
} else {
    success = sendPacket();
}
```

Recommended direction:

- Keep `config.bluetooth.enabled` as radio/app connectivity state.
- Add or reuse a separate runtime request that means "Device UI should yield to programming".
- Make normal `sendPacket()` continue when Bluetooth is merely enabled.
- Preserve the boot-logo long-press path in Device UI as the explicit request to enter Programming Mode.

This is better than special-casing only `HELTEC_V4_R8_TFT`, because the same `USE_PACKET_API` pattern appears in multiple TFT-style targets.

## 12. Expected behavior after patch

After the recommended patch:

- Clean flash still boots into native MUI.
- Enabling Bluetooth from Android keeps Bluetooth enabled after reboot.
- Native MUI still reaches `eRunning` and receives normal `FromRadio` updates.
- Programming Mode is entered only through an explicit Device UI action, such as boot-logo hold/click behavior.
- Leaving Programming Mode disables Bluetooth only if that remains the intended product behavior for the explicit mode.
- Enabling Wi-Fi does not need to be used as an escape hatch for the screen.

## 13. Risks

- The original Programming Mode policy may have been added to avoid two clients competing for the same packet/config API path.
- Enabling normal Device UI traffic while BLE is active may increase heap pressure on ESP32-S3.
- Phone API access control is explicitly incompatible with `USE_PACKET_API`; `PacketAPI.cpp` lines 13-16 fail the build if both are enabled.
- Some other `USE_PACKET_API` targets may rely on the current "Bluetooth means Programming Mode" behavior.
- If the UI sends config writes while a phone app also writes config, transaction/reboot ordering should be tested.
- ESP32 Wi-Fi/BLE radio sharing remains a separate limitation. `src/platform/esp32/main-esp32.cpp` lines 76-81 documents that Wi-Fi and BLE share radio resources.

## 14. Files that would need modification

For the recommended clean fix:

- `src/mesh/api/PacketAPI.h`
  - Add explicit runtime Programming Mode state or setter if needed.
- `src/mesh/api/PacketAPI.cpp`
  - Change `PacketAPI::runOnce()` condition.
  - Keep or adjust `notifyProgrammingMode()`.
- `src/graphics/tftSetup.cpp`
  - Wire any explicit Programming Mode request between Device UI and `PacketAPI`, if the request crosses the firmware/device-ui boundary.
- `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/source/graphics/TFT/TFTView_320x240.cpp`
  - In upstream dependency source, make boot-logo Programming Mode action send an explicit request instead of relying on `bluetooth.enabled`.
- `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/source/graphics/common/ViewController.cpp`
  - If needed, route the explicit request through the client/controller.
- Dependency manifest / package reference for `meshtastic-device-ui`
  - Any actual Device UI source patch should be made upstream or by changing the dependency reference, not by editing `.pio/libdeps` as the final product source.

For a minimal target-only workaround patch:

- `src/mesh/api/PacketAPI.cpp`
  - Add a compile-time exception around the `config.bluetooth.enabled` Programming Mode gate for `HELTEC_V4_R8_TFT`.

That minimal workaround is not the preferred long-term patch because it preserves a confusing coupling between Bluetooth state and Programming Mode on other Device UI targets.

## History notes

Local `git blame` shows the current `PacketAPI::runOnce()` Programming Mode gate at lines 35-51 came from commit `beb268ff25` on 2026-01-04. `git log -S "force client into programmingMode" -- src/mesh/api/PacketAPI.cpp` points to `99d3e5eb7` / "2.6 changes (#5806)" as the introduction of the Programming Mode notification text. The default "switch BT off by default" rule in `NodeDB.cpp` also traces to `99d3e5eb7` / "2.6 changes (#5806)".

No firmware source changes were made for this investigation.
