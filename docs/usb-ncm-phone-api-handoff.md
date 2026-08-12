# USB-Ethernet (CDC-NCM) phone API - working handoff

Branch `feat/usb-ncm-phone-api` (base commit `8f1d253b3`). Pilot env `meshnology_w12_usbnet`, hardware Meshnology W12 (ESP32-S3).

## The goal

Present the node to a USB host as a standard USB Ethernet adaptor, so the Meshtastic iOS app can reach the existing TCP phone API (port 4403) over a plain USB-C cable - including in airplane mode, since the link is not a radio.

USB _serial_ is effectively closed for this app on iPadOS: generic USB-serial devices are not exposed to apps, and the sanctioned route - a custom USBDriverKit dext (M-series iPads only, entitlement-gated) or MFi - died in our spike because Apple's own dexts win IOKit matching and expose no user client (`meshtastic-usbdriverkit-spike`, FINDINGS.md). USB-Ethernet is the only wired route that needs neither, and Apple DTS recommends exactly this shape for an ESP32-S3 ([forums 772812](https://developer.apple.com/forums/thread/772812)); network adaptors are generally not MFi-licensed.

## Status

**The transport works AND the phone API over it works.**

Verified on hardware:

| Check                                              | Result                                                               |
| -------------------------------------------------- | -------------------------------------------------------------------- |
| iPadOS 26 enumerates the gadget, joins the network | ✅                                                                   |
| macOS DHCP lease                                   | ✅ `192.168.7.2/24`                                                  |
| Lease has **no** router option, **no** DNS option  | ✅ `perform_router_discovery: FALSE`                                 |
| Default route not hijacked                         | ✅ still `en0`                                                       |
| ICMP                                               | ✅ ~1.3 ms, 0% loss sustained                                        |
| Port 4403 accepting; 4404 and 80 refuse            | ✅ real listener, `begin()` succeeded                                |
| Interface MAC = `deriveUsbMac()` of chip base      | ✅ `a2:f2:62:e0:e8:91` from `a0:f2:62:e0:e8:90`                      |
| Board stays flashable                              | ✅ many flash cycles                                                 |
| **Real API session completes**                     | ✅ `TCPInterface` + `getMyNodeInfo()`, twice in a row, node stays up |

## Enabling on other boards

The enablement is a reusable fragment: `[usbnet]` in `variants/esp32/usbnet.ini`
carries the build flags, managed components, and sdkconfig any adopter needs,
and documents the adoption recipe (a `<board>_usbnet` env that splices the
fragments in). `[env:meshnology_w12_usbnet]` is the reference consumer.
Eligibility - a native USB-OTG SoC (ESP32-S2/S3/P4) and PSRAM
(`-D BOARD_HAS_PSRAM`) - is enforced at compile time by `#error` guards in
`USBNetEsp32.cpp`, so an ineligible variant fails the build with an
explanation instead of misbehaving on hardware.

## The crash that blocked this branch - root cause and fix

**Symptom:** the node rebooted within seconds whenever a client opened a real
session on 4403. Bare TCP connects and garbage bytes were harmless; a valid
`want_config` was the trigger.

**Coredump verdict** (loopTask, core 1):

```text
operator new (sz=14848) → std::bad_alloc → std::terminate → abort()
  std::vector<meshtastic_FileInfo>::reserve(64)
  getFiles()                    src/FSCommon.cpp:275
  PhoneAPI::handleStartConfig   src/mesh/PhoneAPI.cpp:325
  ← handleToRadio ← readStream ← ServerAPI::runOnce ← loopTask
```

**Root cause chain:**

1. The W12 variant never passed `-D BOARD_HAS_PSRAM`. The framework compiles
   everything with `-DESP32_ARDUINO_LIB_BUILDER`, and `esp32-hal-psram.h` then
   **undefines `CONFIG_SPIRAM`** for the Arduino core - `psramInit()` becomes a
   `return false` stub and `psramAddToHeap()` is never called, no matter what
   the sdkconfig says. The chip's 8 MB embedded PSRAM (esptool reports
   `Embedded PSRAM 8MB (AP_3v3)`) never joined the heap. **Stock W12 has the
   same defect** - it just never allocates hard enough to notice.
2. The usbnet build adds TinyUSB + NCM NTBs + esp_netif + DHCP server + API
   server on top, all in internal SRAM, consuming the remaining margin.
3. First `want_config` → files-manifest walk reserves 64 × 232 B = 14 848 B
   contiguous → allocation fails → `std::bad_alloc`.
4. The OOM guard upstream added around exactly this `reserve` (FSCommon.cpp
   271-293) is **dead code on ESP32**: the build is `-fno-exceptions`, so
   `__cpp_exceptions`/`__EXCEPTIONS` are undefined and the try/catch compiles
   out - while toolchain libstdc++'s `operator new` still throws. Uncaught →
   `std::terminate` → `abort()`.

**Fix:** `-D BOARD_HAS_PSRAM` in `[env:meshnology_w12]` build_flags (one line
plus comment). Verified: the exact repro that reliably rebooted the node now
completes `getMyNodeInfo()` and the node survives back-to-back sessions.

## Build, flash, test

Use the penv PlatformIO core - the Homebrew one is 6.1.18 and **uninstalls the
platform** on sight (needs >= 6.1.19):

```bash
# build
~/.platformio/penv/bin/pio run -e meshnology_w12_usbnet

# flash - ALWAYS pass --upload-port (see traps)
~/.platformio/penv/bin/pio run -e meshnology_w12_usbnet -t upload --upload-port /dev/cu.usbmodem101

# or flash the prebuilt app directly (~25 s)
~/.platformio/penv/bin/python -m esptool --chip esp32s3 --port /dev/cu.usbmodem101 --baud 921600 \
  write_flash 0x10000 .pio/build/meshnology_w12_usbnet/firmware-*.bin
```

Then reset, wait ~30 s for the gadget, and run the gate:

```bash
ifconfig | grep -B5 192.168.7.                # expect 192.168.7.2 on a new enN
USBIF=$(route -n get 192.168.7.1 2>/dev/null | awk '/interface:/{print $2}')
ipconfig getpacket "$USBIF"                   # expect NO router, NO domain_name_server
netstat -rn -f inet | grep default            # must NOT be $USBIF
ping -c3 192.168.7.1
~/.local/pipx/venvs/meshtastic/bin/python -c "import meshtastic.tcp_interface,time; i=meshtastic.tcp_interface.TCPInterface('192.168.7.1'); time.sleep(3); print(i.getMyNodeInfo()); i.close()"
```

(Any python with the `meshtastic` package works for the last line; the pipx venv path is where it lives on the dev machine.)

## Reading a coredump (the recipe that actually worked)

`default_16MB.csv` carries a 64 KB coredump partition at `0xFF0000`. The env
sets `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` (see the trap about `TO_NONE`).

1. Reproduce the crash. The node panics, writes the dump, and reboots.
2. Get a serial port: **replug the cable** (power-on reset) or BOOT+RST. A
   panic reboot alone gives NO serial window - see traps.
3. Read and decode:

```bash
~/.platformio/penv/bin/python -m esptool --chip esp32s3 --port /dev/cu.usbmodem101 --baud 921600 \
  read_flash 0xFF0000 0x10000 core.bin
~/.platformio/penv/bin/python -m esp_coredump info_corefile -c core.bin -t raw \
  --gdb ~/.platformio/packages/tool-xtensa-esp-elf-gdb/bin/xtensa-esp32s3-elf-gdb \
  .pio/build/meshnology_w12_usbnet/firmware-meshnology_w12_usbnet-*.elf
```

The dump survives reboots and reflashes (app partition only), so there is no
race: park the chip whenever convenient and read at leisure.

## Traps that will cost you hours

1. **`pio run -t upload` without `--upload-port` crashes** on a pre-existing repo issue: `AssertionError: Missing target configuration for t-impulse-plus` in the raspberrypi platform's `get_boards()`. Always pass the port.
2. **Do not run `bin/restore-idf-component-yml.sh` before building the usbnet env.** It strips `esp_tinyusb` from the shared framework `idf_component.yml` and the link fails with `cannot find -lespressif__esp_tinyusb`. The platform auto-restores it after a build anyway; the script is belt-and-braces for switching back to other envs.
3. **An existing `sdkconfig.<env>` is authoritative over `custom_sdkconfig`.** Setting `CONFIG_X=n` silently does nothing until you delete that file. This burned two build cycles.
4. **HybridCompile silently reuses cached IDF libs**, so a green build can contain none of your config changes. Symptom: byte-identical flash size and zero relevant symbols. Force a real rebuild: `rm -f sdkconfig.<env> sdkconfig.defaults ~/.platformio/packages/framework-arduinoespressif32-libs/sdkconfig`, then build.
5. **Kconfig _choice_ symbols need their old value explicitly negated.** The platform's sdkconfig merge only rewrites template lines whose flag name appears in `custom_sdkconfig`. Setting `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` alone leaves the template's `CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y` in place, both members of the choice are `=y`, and the later line (TO_NONE) wins - the build silently comes out with no coredump. `CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=n` in the env is load-bearing. (`variants/esp32/esp32-common.ini:169-172` also disables coredump globally; the env-level lines override it because later duplicate flag names win.)
6. **A panic reboot gives NO serial window.** After a software/panic reset the USB pads stay with the OTG controller: the bus is silent for the 30 s start delay and then the NCM gadget re-enumerates directly - USB-Serial-JTAG never appears. Only a power-on reset (replug) or BOOT+RST restores it. Do not try to "race the 30 s window" after a crash; there is nothing to race.
7. **The gadget owns the USB pads once it starts** (normal boots): `USB_NET_START_DELAY_MS` = 30 s keeps a reflash window after power-on/RTS resets. **BOOT+RST parks the chip in ROM download mode with no time limit** - use that when in doubt.
8. **esptool's RTS "hard reset" does not always boot the app** on this board. It worked most of this session, but keep a physical RST press in reserve.
9. **W12 emits no plain-text serial log over USB-Serial-JTAG via raw pyserial** (reads return 0 bytes, reproduced). `tio /dev/tty.usbmodem101` reportedly works; untested this session.
10. **Homebrew's `pio` (6.1.18) is obsolete and destructive** - on first contact it _removed_ the espressif32 platform, which requires core >= 6.1.19. Use `~/.platformio/penv/bin/pio`.
11. **`-D BOARD_HAS_PSRAM` is mandatory on PSRAM boards** - see the crash root cause above. sdkconfig `CONFIG_SPIRAM=y` alone does nothing for Arduino-core builds.

## Load-bearing design decisions - do not "simplify" these

- **Vend no gateway and no DNS.** `gw = 0.0.0.0`, router offer flag cleared, and `CONFIG_LWIP_DHCPS_ADD_DNS=n`. Apple DTS ([forums 779796](https://developer.apple.com/forums/thread/779796)): an accessory DHCP server must not vend a gateway, or the interface becomes the default route. Without the `ADD_DNS` line, `dhcpserver.c`'s else-branch vends the node's own address as a DNS server that answers nothing. This is what stops iOS electing the link, failing its captive probe, and killing it. **Deliberately not warthog's NAT gateway model.**
- **Bring-up order is the correctness argument.** netif + DHCP + API listener all come up in `setup()`; only then does the USB device start and the NCM link get raised. iOS runs DHCP exactly once on link-up and never retries.
- **`tud_network_default_link_state_cb()` is overridden to start the link DOWN**, for the same reason.
- **TinyUSB must resolve to ≥ 0.21.0** (constrained in the shared fragment via `espressif/tinyusb@^0.21.0` - a caret range, not an exact pin; the hardware-verified resolve is `0.21.0~1`). PR #3630 is what makes NCM work on iOS/iPadOS 26; without it DHCP succeeds only ~30% of the time. `esp_tinyusb` alone only requires `>= 0.17.0~2`, so the good resolve was luck.
- **The USB task is pinned to core 0** via `TINYUSB_TASK_CUSTOM(6144, 5, 0)`. esp_tinyusb 2.x moved task config out of Kconfig - `CONFIG_TINYUSB_TASK_STACK_SIZE` and `CONFIG_TINYUSB_TASK_AFFINITY_CPU0` **do not exist** and are silently ignored. Default is core 1 at priority 5, the same core as the Arduino loop task at priority 1.
- **`usbNetTransmit` refuses early and waits only 20 ms.** With `CONFIG_LWIP_TCPIP_CORE_LOCKING=y` it runs inline on the caller under the lwIP core lock, and `tinyusb_net_send_sync` enqueues with an infinite wait. Dropping a frame is free (TCP retransmits); blocking the loop task is not. _But see the backlog item about `tud_network_can_xmit()` below._
- **Never route USB bring-up through `onNetworkConnected()`** - its `displaymode != COLOR` guard would silently kill the feature on exactly the colour-TFT boards someone cables to an iPad.

## Ruled out - don't re-litigate

- **Thread-table exhaustion.** Boot logs `20/40 threads used`; plenty of headroom.
- **HWCDC console blocking the loop task.** The OLED keeps cycling after the gadget starts.
- **iOS app bug / manual-connect path.** Reproduced from macOS with no app.
- **Socket layer / `NetworkServer::begin()` failing.** 4403 accepts while 4404 and 80 refuse.
- **Cross-core NCM TX race as the crash cause.** This was the leading theory
  (three independent code readers converged on it) and the coredump disproved
  it: the panic is a heap-exhaustion abort, not corruption. The underlying
  contract violation is real though - see backlog.
- **tiT/loopTask/TinyUSB-task stack overflows.** Coredump shows a clean
  loopTask abort with healthy stacks, not a canary panic.

## Bugs already found and fixed (all hardware-only, invisible to the build)

1. Zero-initialized `tinyusb_config_t` → task stack size 0, driver refuses. Must use `TINYUSB_DEFAULT_CONFIG()`.
2. Missing `esp_netif_action_start()` / `action_connected()` → netif never comes up, silently answers nothing, host self-assigns 169.254.
3. `action_start` does **not** auto-start dhcps after an explicit `dhcps_stop()` - the explicit fallback is load-bearing (contradicts warthog's comment).
4. `initApiServer()` before any netif exists → null deref on a WiFi-less build (`Not using WIFI` then `LoadProhibited` at `EXCVADDR 0x4c`).
5. The esp_tinyusb Kconfig names above being silently ignored.
6. **Missing `-D BOARD_HAS_PSRAM`** → PSRAM never in heap → `bad_alloc` abort on the first real API session (the crash that blocked this branch).

## Backlog

- **`usbNetTransmit`'s `tud_network_can_xmit()` gate violates TinyUSB's
  threading contract.** In TinyUSB 0.21's NCM driver that call is _not_
  read-only: it rotates NTB ownership (`xmit_setup_next_glue_ntb`), writes NTB
  headers, and can submit `usbd_edpt_xfer` - unlocked, single-context-by-design
  code. esp_tinyusb itself defers its own `can_xmit` into the USB task via
  `usbd_defer_func` (`tinyusb_net.c: do_send_sync`); our gate runs it on the
  lwIP caller (loopTask core 1 / tiT core 0) truly parallel with the TinyUSB
  task (core 0), with `XMIT_NTB_N=1`. Not the crash we chased, but a latent
  corruption risk under sustained TX. Proposed fix: drop the `can_xmit` half of
  the gate (keep `tud_mounted()`, which is a read-only check - exactly what
  esp_tinyusb's own senders do); the bounded 20 ms `send_sync` already provides
  the fail-fast property, from the correct context. Also latent in esp_tinyusb:
  a timed-out `do_send_sync` is never cancelled and services the _next_ packet
  one-behind (dup/drop, not crash).
- Native unit tests for `USBNetPolicy` (deliberately free of Arduino/IDF deps). Assert options 3 and 6 absent and `gw == 0` - the Apple-guidance regression test.
- mDNS on the USB netif. It does **not** auto-join new netifs (predefined set is `WIFI_STA_DEF`/`WIFI_AP_DEF`/`ETH_DEF`); needs `mdns_register_netif()` + `mdns_netif_action(ENABLE_IP4|ANNOUNCE_IP4)`, and there is exactly one free slot at `CONFIG_MDNS_MAX_INTERFACES=3`. Publish TXT `shortname` and `id` or the iOS app renders a blank row.
- Self-powered descriptor at ≤ 50 mA (stock esp_tinyusb descriptor declares bus-powered 100 mA - inside Apple's safe zone, so a refinement not a blocker).
- iPad end-to-end: 20/20 replug lease acquisition, airplane-mode soak, sleep/wake resume, **and a session soak now that sessions work**.
- Decide whether USB should preempt the single TCP session slot (`ServerAPI.h:43-48`).
- Consider whether `MESHNOLOGY_W12` (non-usbnet) users deserve a changelog note: `-D BOARD_HAS_PSRAM` changes stock behavior too (allocations > 4 KB now prefer PSRAM, `psramFound()` becomes true, NodeDB sizing changes).
