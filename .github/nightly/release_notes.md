# Help Test 2.8 - Flash a Nightly, Send Feedback

We're preparing the **2.8** release, and we need your help shaking it out on real hardware. The web flasher now has a **built-in feedback form**. Flash a nightly, use your node like you normally would, and tell us what you find. Every report on a real board moves the release forward.

⚠️ 2.8 nightlies are **experimental, pre-release builds** and can be unstable. Please read the warnings and only flash hardware you can afford to fully erase. Back up your config first.

## Big changes

There are a number of fundamental changes to 2.8 that require a heads-up. Please review some of these higher profile changes so that you are aware of them before installing:

- Node numbers (ID) are now derived from the public-key identity of the node
- XEdDSA based packet signing
- Reduction of long-name to 25 bytes
- Precise position no longer allowed on known-keys (public mesh. Please use private channels for this)
- Telemetry and position are off-by-default and must be opted into
- Ground-up redesigned NodeDB storage and traffic management
- Completely new allocation of LoRa Regions and presets, including ham specific carve-outs

---

## How to help in 3 steps

1. **Flash a 2.8 nightly** from the web flasher onto supported hardware.
2. Make sure your apps / clients are up to date with the latest release in order to support the 2.8 firmware features like packet signing.
3. **Use it normally** for a while - send messages, move around, let it mesh, sleep, and wake. Try the features you actually rely on.
4. **Open the feedback form in the web flasher and tell us what happened** - good, bad, or broken. "Works great on my board" is genuinely useful data too.

---

## Where we most need coverage

The more different boards and setups we hear from, the better. Especially valuable:

- **A variety of supported boards**
- **Both radios and roles** - routers, clients, repeaters, and low-power/sleep configurations.
- **Different regions** and channel/modem-preset combinations.
- **Bluetooth pairing and reconnection** from Android and iOS.
- **Upgrade paths** - flashing 2.8 over an existing 2.x install and confirming your config/keys survive.
- **Peripherals** - GPS, screens (OLED/E-Ink/TFT), sensors, and buttons.

---

## What makes a feedback report actionable

The feedback form captures a lot automatically, but the more of the following you include, the faster we can act:

| Include                   | Why it helps                                                                                  |
| ------------------------- | --------------------------------------------------------------------------------------------- |
| **Exact board / variant** | Behavior is often board-specific (e.g. `rak4631`, `heltec-v3`, `t1000-e`).                    |
| **Build identifier**      | The nightly version / commit / date you flashed, so we can reproduce against the right build. |
| **What you did**          | Concrete steps leading up to the problem - the shorter the repro, the better.                 |
| **Expected vs. actual**   | What you thought would happen, and what actually happened.                                    |
| **How often**             | Every time? Once? Only after a reboot or after X hours?                                       |
| **Logs / screenshots**    | Serial or app logs, and photos of the screen or error, when you have them.                    |
| **Environment**           | Region, phone OS + app version, and anything unusual about your setup.                        |

### Especially flag these

- **Boot loops, hangs, watchdog resets, or unexpected reboots**
- **Config, channel, or key loss** after flashing or upgrading
- **Regressions** - something that worked on your previous (stable) firmware but is now broken
- **Meshing / DM problems** between a 2.8 node and nodes on other firmware
- **Power regressions** - noticeably worse battery life

---

## Good feedback vs. vague feedback

**Vague:** "It's broken, keeps rebooting."

**Actionable:** "On a `rak4631` flashed with the 2.8 nightly from <date/commit>, the node reboots every time I open the Android app (v2.7.15) and tap Position. Happens every time. Serial log attached, region US."

---

> [!NOTE]
> Thank you for testing. Reports from real hardware - including the boring "everything works" ones - are exactly what let us promote 2.8 from nightly preview to a stable release with confidence.

# Changes

### Radio & mesh protocol

- **Packet Signing via XEdDSA** (#10478) plus a **BaseUI signing status UI** (#10841), unsigned-packet policy hardening with test coverage (#10858), and runtime-toggleable `MESHTASTIC_LOCKDOWN` hardening for nRF52 (#10349, opt-in via #10712).
- **Traffic Management Module** for packet forwarding - dedup, rate limiting, role-aware policing (#9358 base, #10706 dedup/rate-limit expansion, #10745 next-hop cache overflow store, #9921 congestion-aware position interval/hop-exhaustion tuning).
- **Automatic variable hop limits** based on live mesh activity and message-size estimation (#10176).
- **Mesh beacon** feature and admin controls (#10618 base implementation, #10839 second pass adding beacon admin/config).
- **Noise floor** tracking with a sliding-window estimate (#9347).
- **Hash table index for O(1) packet history lookups** - improves routing/dedup performance on busy meshes (#9499).
- New amateur-radio regions: 70cm (#10627), 1.25m/125cm (#10638), 2m/~144MHz (#10623); EU regions merge plus Narrow/Lite region enablement for EU (#10675, #10120); LoRa region preset map for cleaner per-region defaults (#10736).
- **TinyFast and TinySlow modem presets** added to config and menu (#10597).
- LoRa config changes now apply live without a reboot (#9962); LoRa settings expansion and validation improvements (#9878).
- Position privacy: direct-send position packets are clamped to channel precision (#10383), and public/known-key position precision is clamped similarly (#10665) and honors explicit channel settings to prevent location leaks (#10513).
- Licensed operators are now prevented from rebroadcasting packets to/from unlicensed users, for regulatory compliance (#9958).
- Packets with missing/invalid `hop_start` (pre-hop firmware) are now deprecated/blocked (#9476).
- Spoof detection added for UDP multicast packets (#9905).
- Low-bandwidth conversion support added to MeshRadio (#10595).
- ATAK Plugin V2 implemented (drops legacy unishox2 compression) (#10105), including TAKTALK voice/text chat message and room data structures.
- Ethernet HTTP/HTTPS API server ported to RP2350 + W5500 boards (#10573), plus Ethernet OTA support for RP2350/W5500 (#10136).
- Optional `LED_LORA` indicator to show LoRa TX activity (#10465), and an LED indicator for LoRa RX (#10674).
- GPS time sync: device now sets its clock from GPS every 30 minutes (#10737); GPS is skipped at startup if the LoRa region is unset, to save battery (#10386); GPS model/baudrate now cached across reboots to skip a full sweep (#10544).
- Config: **position & telemetry broadcast are now opt-in** rather than always-on (#10929).
- **Extra-repeat tolerance** - device tolerates a configurable number of heard repeats before cancelling its own rebroadcast, with tolerance suppressed when the mesh is busy or dense (recent local branch work).

### UI / display - BaseUI

_BaseUI is the current default graphical UI (`src/graphics/Screen.cpp`, `draw/UIRenderer`, `draw/MenuHandler`, `draw/CompassRenderer`, `draw/NotificationRenderer`, `draw/NodeListRenderer`)._

- **"Ham Mode"** - first implementation (#10663).
- **Color support for TFT-equipped nodes** (#10233).
- Status-message display added to Favorite/NodeList screens (#9504, #10197).
- **Hex picker** UI added for entering hex values (#10650).
- Save/restore of frame visibility state across sessions (#10576).
- BLE pairing PIN now shown via a proper on-screen banner (#8902).
- Compass rendering/behavior improvements (#10166).
- Emote handling refactor (#9896).

### UI / display - InkHUD

_InkHUD is the e-ink-optimized UI (`src/graphics/niche/InkHUD`)._

- **Offline map tiles with zoom controls** (#10785) and general GPS UX improvements (#10846).
- **Full touch support** for the T5 E-Paper S3 (#10286) and a full InkHUD port for the LilyGo T5 E-Paper S3 Pro (#10211).
- **"Wipe all messages"** option (#10721).
- T-mini E-Ink S3 support added (#9856).

### UI / display - MUI

_MUI is the older/legacy graphical UI menu system, still selectable alongside BaseUI (`draw/MenuHandler`'s `MuiPicker`/`switchToMUIMenu`)._

- WiFi map-tile download adapted for Heltec V4 (#10011).

### UI / display - shared / cross-cutting

_Touches the display driver layer or more than one UI system at once._

- InkHUD and BaseUI **message store unified** into a shared implementation (#10596).
- T-mini E-Ink S3 support landed for both InkHUD and BaseUI (#9856).
- Board-specific TFT driver support and simplified TFT ifdef chains for easier porting (#10827, #10803); faster TFT color conversion (#10814); touchscreen variant flags (`VARIANT_TOUCHSCREEN`/`ENABLE_TOUCH_INT`) added for new touch boards (#10815).

## Backend / developer-facing features

### Memory & stability infrastructure

- **MemClass.h** - a central memory-class ladder providing fail-safe-small defaults across constrained platforms (#10901).
- **MemAudit** - per-subsystem heap accounting reported in the boot log (#10900).
- nRF52 heap tiers and SoftDevice RAM reservation right-sized, freeing an extra ~8KB heap arena (#10898, #10903).
- Native Portduino malloc shim added for more realistic memory behavior in simulation (#10677).
- Bluetooth memory freed automatically when Wi-Fi is enabled or Bluetooth is disabled (#10398); Bluetooth wait is skipped entirely when disabled (#10571).
- NodeDB "warm store" - new persistent warm-tier node storage layer (#10705 base, #10746/#10759 right-sizing for constrained platforms, #10809 fixing associated spiLock deadlocks).
- 2.8 NodeDB shrink/decoupling/restructuring to reduce per-node memory footprint (#10413).
- Flash hardening, filesystem platform unification, and a write-behind LFS cache for STM32WL/nRF52 - a storage-format break (#10171).

### New APIs / module-author infrastructure

- **MCP server** added for interacting with Meshtastic devices and driving a testing framework / TUI, later extracted to its own repo (#10194, #10861).
- Native "sensors" simulation support for Portduino (#10748); `PortduinoSetOptions` to override the `realhardware` flag for tests (#10157).
- Hardfault handler added for STM32, making crashes visibly obvious in logs (#10071).
- `BinarySemaphorePosix` implemented with proper pthread synchronization for native builds (#9895).
- NimBLE parameter overhaul, including a fix attempt for incompatible BLE bond cleanup (#10741).
- STM32 ADC support added to `AnalogBatteryLevel` (#9369).
- JSON library dependency removed from firmware builds while retaining full JSON support in meshtasticd (#10152) - reduces firmware footprint for module authors relying on the JSON path.
- Improved manual build flow / developer build ergonomics (#8839).- External Notifications module logic fully reworked for more flexible notification rules (#10006).
