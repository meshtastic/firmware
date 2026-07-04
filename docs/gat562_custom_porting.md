# GAT562 Custom Porting Notes

This file records the GAT562-specific features that are not in upstream
Meshtastic firmware. When rebasing or forking a newer upstream release, keep
these changes as a separate patch set and re-validate them on hardware.

## Hardware Definitions

- Board: `gat562_mesh_trial_tracker`
- GPS enable: GPIO 34, active high
- Buzzer: P1.01
- WS2812 notification LED: P0.29
- Joystick: left, right, up, down, OK
- USER key keeps the original bootloader behavior: hold USER while powering on
  via USB must still enter bootloader.

## Features Added On Top Of Upstream

- Joystick navigation and input flow
  - Left/right switch UI pages.
  - OK enters menus and selected actions.
  - Long OK enters/exits game mode only from normal UI pages.
  - Preset message editing must not trigger game mode.

- Chinese and English input
  - Virtual keyboard supports EN and CN modes.
  - CN mode uses pinyin candidate selection.
  - Chinese display support is required both in compose/input UI and message
    display/history UI.
  - Font resources are under the GAT562 variant and graphics code.

- Preset messages
  - 20 editable preset messages.
  - Existing preset content is preserved.
  - Empty preset slots are filled with `A` to `Z` placeholders.
  - Presets can be selected and sent from compose flow.
  - Presets can be edited with the virtual keyboard.

- Message history
  - Store recent message history for the message page.
  - Keep up to 20 records.
  - Total stored text budget must stay within about 400 bytes.
  - When over budget, remove oldest records first.

- Buzzer notification
  - Key press beep.
  - Send-confirm melody from joystick send flow.
  - Received-message ringtone uses the short Nokia-style melody timing.
  - Audio must be non-blocking and must not delay mesh send/receive timing.

- WS2812 notification LED
  - LED must stay off after boot.
  - On received LoRa message, blink/breathe white for the same notification
    window as the ringtone.
  - Do not leave green or any other color latched after boot or after notify.

- Games
  - Four mini games are added from MeshCore/GAT562 custom firmware.
  - Game mode is entered by long OK only on normal UI pages.
  - Skull game controls: OK is punch/action, USER is jump/A behavior.
  - Rendering should avoid heavy blocking work so UI remains responsive.

- GAT562 UI pages
  - Boot logo uses local GAT-IoT logo asset.
  - UI first page shows GAT562 BLE name using the device suffix.
  - System page shows RSSI/SNR, uptime/version, and app connection state.
  - LoRa page includes live Noise value and follows the tested layout.
  - Clock page is added at the end of the UI pages.
  - Screen wake on received message, then follow normal screen timeout.

- BLE behavior
  - Keep upstream BLE connection/security/service logic intact.
  - BLE advertising/pairing name is always fixed as `GAT562_xxxx`, using the
    device suffix.
  - If the phone app changes the device long name, reboot must preserve and
    display the user name on the app and terminal UI instead of forcing
    `GAT562_xxxx` again.
  - GAT562 may customize displayed name and pairing screen, but must not fork
    the core BLE connection path.
  - Previous connection failure was caused by deviating from upstream BLE logic;
    future rebases should preserve official BLE initialization and pairing
    behavior unless a hardware-specific change is proven necessary.

- GPS behavior
  - GPS config defaults must force the GAT562 UART pins and GPIO 34 enable pin.
  - GPS enable pin is active high and should be driven high when enabled.
  - Default GPS update interval is 120 seconds. Do not force 10 seconds because
    that keeps GPS in the always-on threshold and drains power quickly.
  - Reboot should clear stale GPS display state and acquire a fresh lock.
  - After a valid lock, a later refresh/search timeout must not publish an empty
    GPS status that erases the last valid fix from NodeDB/UI.
  - Keep upstream GPS scheduling/search behavior as much as possible.

- Region/frequency presets
  - Region preset selection is joystick accessible.
  - CN preset frequency is the tested GAT562 custom value.
  - TW/channel persistence must be retained across reboot.

## Main Files To Recheck After Upstream Sync

- `variants/nrf52840/gat562_mesh_trial_tracker/variant.h`
- `variants/nrf52840/gat562_mesh_trial_tracker/platformio.ini`
- `variants/nrf52840/gat562_mesh_trial_tracker/gat_iot_logo.h`
- `variants/nrf52840/gat562_mesh_trial_tracker/gat562_utf8_10x10.h`
- `src/gps/GPS.cpp`
- `src/mesh/NodeDB.cpp`
- `src/platform/nrf52/NRF52Bluetooth.cpp`
- `src/graphics/Screen.cpp`
- `src/graphics/VirtualKeyboard.cpp`
- `src/graphics/VirtualKeyboard.h`
- `src/graphics/GAT562Arcade.cpp`
- `src/graphics/GAT562Arcade.h`
- `src/graphics/GAT562NotifyLed.cpp`
- `src/graphics/GAT562NotifyLed.h`
- `src/graphics/draw/UIRenderer.cpp`
- `src/graphics/draw/MenuHandler.cpp`
- `src/graphics/draw/NotificationRenderer.cpp`
- `src/graphics/draw/DebugRenderer.cpp`
- `src/modules/CannedMessageModule.cpp`
- `src/modules/CannedMessageModule.h`
- `src/modules/ExternalNotificationModule.cpp`
- `src/modules/ExternalNotificationModule.h`
- `src/modules/OnScreenKeyboardModule.cpp`
- `src/modules/TextMessageModule.cpp`
- `src/modules/Modules.cpp`
- Pinyin resources under `src/graphics/pinyin_*`
- Extra local libraries under `lib/`

## Regression Checklist

After every upstream merge, test these on real hardware:

1. Boot logo and first UI page display correctly.
2. BLE name and PIN pairing match the phone app, and the app connects.
3. USB bootloader still works by holding USER during USB power-on.
4. GPS powers on, gets a fresh lock after reboot, and does not fall back to
   `NO GPS LOCK` after sitting with a previous valid lock.
5. LoRa send/receive is immediate compared with official firmware.
6. Joystick send plays the tested send-confirm melody.
7. Received message wakes screen, plays ringtone, and blinks white LED.
8. CN/EN input, Chinese rendering, and pinyin candidates work.
9. Preset message select/send/edit works without reboot or game conflict.
10. Game entry/exit and game buttons work, especially Skull Island OK action.
11. TW/CN region presets persist across reboot.
12. Clock page and one-minute screen timeout still behave as expected.
