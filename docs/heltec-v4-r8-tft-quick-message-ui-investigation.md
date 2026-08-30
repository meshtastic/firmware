# Heltec V4 R8 TFT Quick Message UI Investigation

Target: `heltec-v4-r8-tft` / Heltec WiFi LoRa 32 V4 R8 Expansion Kit V2 TFT touchscreen.

Firmware source inspected locally from this checkout. No firmware or device-ui source was modified for this investigation.

## 1. Executive summary

The native touchscreen UI for this target is the Meshtastic `device-ui` LVGL UI, pulled into firmware as a PlatformIO library dependency. The target compiles with `HAS_TFT=1`, `HAS_TOUCHSCREEN=1`, `USE_PACKET_API`, `VIEW_240x320`, `DISPLAY_SET_RESOLUTION`, and `LGFX_DRIVER=LGFX_HELTEC_V4_TFT` in `variants/esp32s3/heltec_v4_r8/platformio.ini:36`.

The best architecture for a cycling quick-message UI is to add a dedicated LVGL panel to the Device UI, then send preset text through the existing `ViewController::sendTextMessage()` path. That path already knows how to create Meshtastic `TEXT_MESSAGE_APP` mesh packets, log local outgoing messages, and handle response status coloring.

Do not edit `.pio/libdeps/.../meshtastic-device-ui` as the permanent source of truth. It is a generated dependency cache. For real work, fork or branch `meshtastic/device-ui`, point `[device-ui_base]` in firmware `platformio.ini` at the fork or a pinned zip/commit, and keep firmware-side changes minimal.

## 2. UI architecture overview

Firmware starts the TFT Device UI from `src/graphics/tftSetup.cpp`. `tftSetup()` creates `DeviceScreen`, creates `PacketAPI`, then calls `deviceScreen->init(new PacketClient)` in `src/graphics/tftSetup.cpp:323`. The TFT task later runs `deviceScreen->task_handler()` in `src/graphics/tftSetup.cpp:314`.

`DeviceScreen` owns the GUI instance. Its constructor selects a concrete view through `ViewFactory::create()` in `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/source/graphics/DeviceScreen.cpp:38`.

For this target, `VIEW_240x320` still maps to the `TFTView_320x240` implementation:

- `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/source/graphics/common/ViewFactory.cpp:11` includes `TFTView_320x240.h` for `VIEW_240x320`.
- `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/source/graphics/common/ViewFactory.cpp:37` returns `TFTView_320x240::instance()` for `VIEW_240x320`.

The actual panel driver is `LGFX_HELTEC_V4_TFT`. It reports `screenWidth = 240` and `screenHeight = 320` in `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/include/graphics/LGFX/LGFX_HELTEC_V4_TFT.h:100`, and the target defines `DISPLAY_SET_RESOLUTION` in `variants/esp32s3/heltec_v4_r8/platformio.ini:85`.

## 3. Screen/state diagram

The runtime flow is:

```text
src/main.cpp
  -> tftSetup()
    -> DeviceScreen::create()
      -> ViewFactory::create()
        -> TFTView_320x240::instance()
    -> PacketAPI::create(PacketServer::init())
    -> DeviceScreen::init(new PacketClient)
      -> TFTView_320x240::init()
        -> ui_init_boot()
        -> wait for DeviceUIConfig / boot state
        -> setupUIConfig()
          -> init_screens()
            -> ui_init()
            -> ui_set_active(home_button, home_panel, top_panel)
            -> lv_screen_load_anim(main_screen)
```

The UI state enum is in `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/include/graphics/common/MeshtasticView.h:35`. Relevant states include `eBooting`, `eBootScreenDone`, `eSetupUIConfig`, `eInitScreens`, `eInitDone`, `eRunning`, `eScreenSaving`, and `eDisconnected`.

`TFTView_320x240::setupUIConfig()` sets state and calls `init_screens()` once the UI config is valid in `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/source/graphics/TFT/TFTView_320x240.cpp:202`.

## 4. Current screen order

There is not a normal ordered carousel of separate app screens. The generated LVGL screens are only boot/main/blank/lock/calibration, defined by `ScreensEnum` in `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/generated/ui_320x240/screens.h:69`.

The main app is one LVGL `main_screen` containing multiple panels. Navigation is done by calling `ui_set_active(button, panel, topPanel)`, which hides the old active panel and shows the new one in `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/source/graphics/TFT/TFTView_320x240.cpp:460`.

The side navigation order in the generated UI is:

1. Home
2. Nodes
3. Groups
4. Messages
5. Map
6. Settings

Those buttons are created in `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/generated/ui_320x240/screens.c:305`, `:318`, `:330`, `:342`, `:354`, and `:366`.

## 5. Current home screen implementation

The current home screen is `objects.home_panel`, created at `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/generated/ui_320x240/screens.c:380`.

On this 240x320 portrait display, the home panel uses:

- Position: `LV_PCT(12), LV_PCT(10)` at `screens.c:383`.
- Size: `LV_PCT(88), LV_PCT(90)` at `screens.c:384`.
- Approximate usable area: 211 x 288 pixels.

Inside it, `objects.home_container` is a flex row-wrap container, created at `screens.c:393`. The first home tile, `objects.home_mail_button`, is a 36 x 36 button at `screens.c:424`.

The default active panel is set in `TFTView_320x240::init_screens()`:

- `activeMsgContainer = objects.messages_container` at `.pio/.../TFTView_320x240.cpp:363`.
- `ui_set_active(objects.home_button, objects.home_panel, objects.top_panel)` at `.pio/.../TFTView_320x240.cpp:372`.
- `lv_screen_load_anim(objects.main_screen, ...)` at `.pio/.../TFTView_320x240.cpp:376`.

## 6. Touch/navigation architecture

Touch input is an LVGL pointer input device through the LovyanGFX driver. The target defines `CUSTOM_TOUCH_DRIVER`, `TOUCH_I2C_PORT=0`, and `TOUCH_SLAVE_ADDRESS=0x2E` in `variants/esp32s3/heltec_v4_r8/platformio.ini:96`.

The touch implementation is in `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/include/graphics/LGFX/LGFX_HELTEC_V4_TFT.h`:

- `chsc6x.h` is included at line `20`.
- The driver constructs `chsc6x` on `Wire` when `TOUCH_I2C_PORT != 1` at line `36`.
- `getTouchXY()` reads raw touch info and rotates coordinates at line `73`.

Event handlers are attached in `TFTView_320x240::ui_events_init()`. The main screen gets an `LV_EVENT_GESTURE` handler at `.pio/.../TFTView_320x240.cpp:891`, but the gesture handler only acts when `activePanel == objects.map_panel` in `.pio/.../TFTView_320x240.cpp:2454`. It maps swipe direction to map pan commands. I found no source-backed global left/right screen cycling gesture.

Long press timing and pointer setup are in `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/include/graphics/driver/LGFXDriver.h`, where `defaultLongPressTime` is 700 ms and `defaultGestureLimit` is 10.

## 7. Existing text-message send path

The current typed-message send flow is:

```text
LVGL textarea ready event
  -> TFTView_320x240::ui_event_message_ready()
    -> TFTView_320x240::handleAddMessage()
      -> ViewController::sendTextMessage()
        -> ViewController::send(... TEXT_MESSAGE_APP ...)
          -> PacketClient::send(ToRadio)
            -> PacketAPI / MeshService
```

Exact source points:

- `message_input_area` is created as a one-line textarea with max length 220 in `.pio/.../generated/ui_320x240/screens.c:1256`.
- `ui_event_message_ready()` reads the textarea and calls `handleAddMessage(txt)` in `.pio/.../TFTView_320x240.cpp:1709`.
- `handleAddMessage()` chooses broadcast/channel or direct-node destination from `activeMsgContainer->user_data` in `.pio/.../TFTView_320x240.cpp:4536`.
- For a channel message, `to` remains `UINT32_MAX`, `ch` is the channel index, and a text response request is created at `.pio/.../TFTView_320x240.cpp:4550`.
- It calls `controller->sendTextMessage(to, ch, hopLimit, actTime, requestId, usePkc, msg)` at `.pio/.../TFTView_320x240.cpp:4570`.
- It adds the outgoing message bubble locally at `.pio/.../TFTView_320x240.cpp:4571`.

`ViewController::sendTextMessage()` is in `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/source/graphics/common/ViewController.cpp:473`. It sends portnum `meshtastic_PortNum_TEXT_MESSAGE_APP` at line `479` and logs the outgoing message at line `481`.

The generic packet builder sets `.to`, `.channel`, `.id`, `.hop_limit`, `.want_ack`, and `.pki_encrypted` in `.pio/.../ViewController.cpp:534`.

## 8. Existing received-message path

Incoming packets reach Device UI through `ViewController::receive()` in `.pio/.../ViewController.cpp:586`. It calls `handleFromRadio()` at line `594`.

`ViewController::handleFromRadio()` routes decoded packets to `packetReceived(p)` in `.pio/.../ViewController.cpp:686`.

`ViewController::packetReceived()`:

- Calls `view->packetReceived(p)` first at `.pio/.../ViewController.cpp:915`.
- Handles `ALERT_APP`, `DETECTION_SENSOR_APP`, `TEXT_MESSAGE_APP`, and `RANGE_TEST_APP` as text-like messages at `.pio/.../ViewController.cpp:925`.
- Calls `view->newMessage(p.from, p.to, p.channel, ..., time)` at `.pio/.../ViewController.cpp:946`.
- Logs the received message at `.pio/.../ViewController.cpp:947`.

`TFTView_320x240::newMessage()` builds or finds the per-channel/per-node message container in `.pio/.../TFTView_320x240.cpp:6515`. For group messages, it prepends the sender short name or node suffix at `.pio/.../TFTView_320x240.cpp:6521`. If the message is not being restored and the user is not already viewing that chat, it increments unread count and may call `showMessagePopup()` at `.pio/.../TFTView_320x240.cpp:6549`.

`showMessagePopup()` sets popup text, stores channel/node in `objects.msg_popup_button->user_data`, unhides the popup, and focuses it in `.pio/.../TFTView_320x240.cpp:6814`.

## 9. Canned Message / quick-message functionality already present

The firmware already contains `CannedMessageModule`, but it is not currently active for the color Device UI path.

The module is declared in `src/modules/CannedMessageModule.h:52`. It supports up to 50 split messages with an 800-byte internal buffer in `src/modules/CannedMessageModule.h:27`.

Its persistent config file is `/prefs/cannedConf.proto`, set in `src/modules/CannedMessageModule.cpp:60`. Default messages are installed in `CannedMessageModule::installDefaultCannedMessageModuleConfig()` at `src/modules/CannedMessageModule.cpp:2311`.

The protobuf storage type is tiny: `meshtastic_CannedMessageModuleConfig` contains `char messages[201]` in `src/mesh/generated/meshtastic/cannedmessages.pb.h:14`, separated by `|`.

However, `Modules.cpp` only creates `CannedMessageModule` when the display mode is not color:

- `src/modules/Modules.cpp:210` checks `HAS_SCREEN && !MESHTASTIC_EXCLUDE_CANNEDMESSAGES`.
- `src/modules/Modules.cpp:211` requires `config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR`.
- `src/modules/Modules.cpp:212` creates `new CannedMessageModule()`.

Device UI has hooks for canned-message config, but they are effectively no-ops:

- `ViewController::sendConfig(meshtastic_ModuleConfig_CannedMessageConfig &&...)` exists at `.pio/.../ViewController.cpp:367`.
- Received canned-message module config calls `view->updateCannedMessageModule(cfg)` at `.pio/.../ViewController.cpp:821`.
- `MeshtasticView::updateCannedMessageModule()` is a virtual no-op in `.pio/.../include/graphics/common/MeshtasticView.h:113`.
- `TFTView_320x240` overrides it with an empty body in `.pio/.../include/graphics/view/TFT/TFTView_320x240.h:58`.

Conclusion: use the canned-message storage model later, but do not expect the existing module UI to render on this TFT target without additional work.

## 10. Recommended architecture for cycling quick messages

Recommended path: add a new `quick_messages_panel` to the Device UI and route each preset button through the existing typed-message send machinery.

The least risky implementation is:

1. Keep existing `messages_panel`, `groups_panel`, and `home_panel` behavior intact.
2. Add a dedicated quick-message panel in `device-ui`.
3. Make it the default visible panel for this fork/target if the cycling device should boot straight to large buttons.
4. Keep old Home reachable from the side navigation or a small status/action tile.
5. Send via a small helper that uses the same destination/channel/request/logging logic as `handleAddMessage()`.

For channel broadcast, use the same semantics as current group chat:

- Destination: `UINT32_MAX` / `NODENUM_BROADCAST`.
- Channel: initially `0` or the currently selected channel.
- Hop limit: `db.config.lora.hop_limit`.
- Portnum: `meshtastic_PortNum_TEXT_MESSAGE_APP`.

Best code shape for Phase 1 is to refactor `handleAddMessage()` into a reusable send helper, rather than faking textarea input. For example, conceptually:

```cpp
sendQuickMessage(uint8_t ch, const char *message)
```

Internally it should create a `ResponseHandler::TextMessageRequest`, call `controller->sendTextMessage()`, and add the local outgoing bubble exactly like `.pio/.../TFTView_320x240.cpp:4570`.

## 11. Suggested first quick-message screen layout

For this 240x320 portrait screen, the existing app panel area is about 211 x 288 pixels. A cycling UI should prioritize big touch targets over many options.

Recommended first layout:

- Existing side nav remains at left.
- Existing top panel area remains available for title/status.
- Quick panel uses the current content rectangle: x 12%, y 10%, w 88%, h 90%.
- Use a 2 x 3 grid of large buttons.
- Button target size: roughly 90-96 px wide and 66-76 px tall.
- Use short visible labels, with the transmitted message stored separately when useful.

This is more usable with gloves and movement than a dense 2 x 4 grid. If eight messages are required immediately, use two pages of four or a 2 x 4 grid with very short labels only.

## 12. Suggested initial message list

Suggested Finnish cycling presets:

1. `OK`
2. `Pysahdyn`
3. `Tauko`
4. `Tarvitsen apua`
5. `Olen tulossa`
6. `Missä olet?`

Suggested transmitted text can be slightly longer than button labels:

- Label `OK`, send `OK`
- Label `Pysahdyn`, send `Pysahdyn hetkeksi`
- Label `Tauko`, send `Pidetaan tauko`
- Label `Apua`, send `Tarvitsen apua`
- Label `Tulossa`, send `Olen tulossa`
- Label `Missä?`, send `Missä olet?`

Note: the LVGL Montserrat fonts appear to include Latin-1 range in generated font files, so Finnish letters are likely available. If minimizing risk for the first firmware test, use ASCII labels/messages first: `Pysahdyn`, `Pidetaan tauko`, `Missa olet?`.

## 13. How message presets should eventually be stored/configured

Best long-term storage: reuse the existing canned-message protobuf config at `/prefs/cannedConf.proto`.

Reasons:

- It already exists in firmware.
- It is already exposed through admin messages.
- It stores `|`-separated messages in `meshtastic_CannedMessageModuleConfig.messages`.
- It avoids adding a new JSON parser or another settings file format.

The Device UI already receives canned-message module config through `ViewController::handleFromRadio()` and calls `updateCannedMessageModule(cfg)` at `.pio/.../ViewController.cpp:821`. The missing piece is implementing that method for `TFTView_320x240`, parsing the message string, and redrawing/updating quick-message buttons.

A separate `/quickmessages.json` on LittleFS is possible, but less aligned with the existing codebase. Device UI does use LittleFS directly: `TFTView_320x240.cpp` binds `fileSystem = LittleFS` at `.pio/.../TFTView_320x240.cpp:42`, and `ViewController.cpp` binds `persistentFS = LittleFS` at `.pio/.../ViewController.cpp:13`. Still, protobuf config is cleaner here.

## 14. How incoming quick messages could be emphasized

The easiest UI emphasis point is `TFTView_320x240::newMessage()` in `.pio/.../TFTView_320x240.cpp:6515`, because it already has UI context and already decides whether to show a popup.

Options:

- If incoming text exactly matches one of the quick-message presets, set a stronger popup label such as `Quick: Apua` before calling or inside `showMessagePopup()`.
- Use the existing `msg_popup_panel` as the first phase: it is already a centered 85% x 55 px alert-like panel in generated UI at `.pio/.../generated/ui_320x240/screens.c:6131`.
- For urgent messages like `Tarvitsen apua`, use `messageAlert()` or a new urgent popup style. `messageAlert()` only sets text and hides/shows `objects.alert_panel` at `.pio/.../TFTView_320x240.cpp:5780`.
- Consider triggering display wake via the same mechanism already present in `showMessagePopup()`: it calls `lv_disp_trig_activity(NULL)` when `db.module_config.external_notification.alert_message` is set at `.pio/.../TFTView_320x240.cpp:6827`.

Do not implement keyword emphasis in `ViewController::packetReceived()` unless the logic must be shared across multiple views. Device-specific visual behavior belongs closer to `TFTView_320x240`.

## 15. Exact files that would eventually need modification

Device UI fork/branch:

- `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/generated/ui_320x240/screens.h`
- `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/generated/ui_320x240/screens.c`
- `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/source/graphics/TFT/TFTView_320x240.cpp`
- `.pio/libdeps/heltec-v4-r8-tft/meshtastic-device-ui/include/graphics/view/TFT/TFTView_320x240.h`

But these paths are the local dependency cache. The actual editable source should be the corresponding files in a fork of `https://github.com/meshtastic/device-ui`.

Firmware repo:

- `platformio.ini`, specifically `[device-ui_base]` at `platformio.ini:137`, to point at the fork/pinned commit instead of `https://github.com/meshtastic/device-ui/archive/9d9b9df81fcde646811a10942d00d5f45f72af7b.zip` at `platformio.ini:140`.
- Possibly `variants/esp32s3/heltec_v4_r8/platformio.ini` only if the quick-message UI is target-gated with a new build flag.

Optional later firmware integration:

- `src/modules/CannedMessageModule.*` only if making the canned module active under `COLOR`.
- `src/graphics/niche/Utils/CannedMessageStore.cpp` is worth studying for reusable admin/storage behavior, because it already handles canned-message config separately from the module UI.

## 16. Recommended development/fork strategy for meshtastic-device-ui

Use a device-ui fork or branch, not `.pio/libdeps` edits.

Recommended workflow:

1. Fork `meshtastic/device-ui`.
2. Create a branch such as `heltec-v4-r8-quick-messages`.
3. Implement the panel and send helper in that repo.
4. Pin firmware `platformio.ini` `[device-ui_base]` to a specific fork commit or zip.
5. Rebuild `heltec-v4-r8-tft`.
6. Test on the second identical device first.
7. Keep a minimal firmware patch that only changes dependency pinning and, if needed, target flags.

This keeps changes reviewable and avoids losing work when PlatformIO refreshes `.pio/libdeps`.

## 17. Risks

The main risks are:

- Generated UI files may be regenerated from the upstream Device UI authoring tool, so manual edits to generated `screens.c/h` can be overwritten.
- `VIEW_240x320` uses a class and generated folder named `TFTView_320x240` / `ui_320x240`; this is source-backed behavior, but it can be confusing during edits.
- The content area is small, so too many quick buttons will produce unsafe touch targets.
- The existing `CannedMessageModule` is not instantiated for `COLOR` display mode, so reusing its UI directly is not a small patch.
- Incoming-message emphasis must avoid making all text messages noisy.
- Changes to Device UI can affect other `VIEW_320x240` or `VIEW_240x320` targets unless guarded carefully.
- A bad display/touch driver change can produce a black screen or unusable UI even if firmware boots.

## 18. Recommended Phase 1 implementation plan

Phase 1 should be deliberately small:

1. Create a device-ui fork/branch and point firmware `[device-ui_base]` at it.
2. Add a quick-message panel to `TFTView_320x240` for `VIEW_240x320`.
3. Use six static presets.
4. Make the quick-message panel the default active panel on this target.
5. Keep the existing Messages screen and typed input unchanged.
6. Refactor the send code so quick buttons call the same `ViewController::sendTextMessage()` path as typed messages.
7. On successful send tap, add the same local outgoing bubble and briefly show an existing alert/popup-style confirmation.
8. Build only `heltec-v4-r8-tft`.
9. Flash the second identical device first.
10. Verify: screen lights, touch works, each preset sends, receiving node sees `TEXT_MESSAGE_APP`, outgoing messages appear in chat history, and normal Messages/Map/Settings navigation still works.

Phase 2 can add canned-message config parsing through `updateCannedMessageModule()`. Phase 3 can add incoming quick-message emphasis and urgent-message styling.
