# USB-Ethernet (CDC-NCM) phone API - working handoff

Branch `feat/usb-ncm-phone-api` (base commit `8f1d253b3`). Pilot env `meshnology_w12_usbnet`, hardware Meshnology W12 (ESP32-S3).

## The goal

Present the node to a USB host as a standard USB Ethernet adaptor, so the Meshtastic iOS app can reach the existing TCP phone API (port 4403) over a plain USB-C cable - including in airplane mode, since the link is not a radio.

USB _serial_ is permanently closed on iPadOS: Apple's own dexts win IOKit matching and expose no user client (`meshtastic-usbdriverkit-spike`, FINDINGS.md). USB-Ethernet is the only wired route, and Apple DTS recommends exactly this shape for an ESP32-S3 ([forums 772812](https://developer.apple.com/forums/thread/772812)); network adaptors are generally not MFi-licensed.

## Status

**The transport works. The phone API over it does not, yet.**

Verified on hardware:

| Check                                              | Result                                          |
| -------------------------------------------------- | ----------------------------------------------- |
| iPadOS 26 enumerates the gadget, joins the network | ✅                                              |
| macOS DHCP lease                                   | ✅ `192.168.7.2/24`                             |
| Lease has **no** router option, **no** DNS option  | ✅ `perform_router_discovery: FALSE`            |
| Default route not hijacked                         | ✅ still `en0`                                  |
| ICMP                                               | ✅ ~1.3 ms, 0% loss sustained                   |
| Port 4403 accepting; 4404 and 80 refuse            | ✅ real listener, `begin()` succeeded           |
| Interface MAC = `deriveUsbMac()` of chip base      | ✅ `a2:f2:62:e0:e8:91` from `a0:f2:62:e0:e8:90` |
| Board stays flashable                              | ✅ ~10 flash cycles                             |

**Open bug: the node reboots when a client opens a real session on 4403.**

- It is firmware, not the app - reproduces from macOS with no Meshtastic app involved.
- It needs a _real_ TCP session. Connecting to `192.168.7.99` (nothing there) does not trigger it.
- The reboot is fast (seconds), not a 90 s watchdog timeout.

## Next step: get the actual panic

Stop theorising - three theories have already died to measurement (see "Ruled out"). Get the coredump.

1. Build and confirm the coredump config actually applied:

```bash
pio run -e meshnology_w12_usbnet
grep ESP_COREDUMP_ENABLE sdkconfig.meshnology_w12_usbnet
```

You want `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`. If it says `..._TO_NONE=y`, the IDF libs were reused from cache - force a real rebuild:

```bash
rm -f sdkconfig.meshnology_w12_usbnet
rm -f ~/.platformio/packages/framework-arduinoespressif32-libs/sdkconfig
pio run -e meshnology_w12_usbnet
```

(Removing the package-level `sdkconfig` makes `flag_any_custom_sdkconfig` false in the platform's `arduino.py`, so `call_compile_libs()` fires unconditionally. This is the reliable lever.)

2. BOOT+RST, flash, plain RST, reproduce the crash, BOOT+RST, then read the dump:

```bash
python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodem101 read_flash 0xFF0000 0x10000 core.bin
python3 -m esp_coredump info_corefile -c core.bin -t raw \
  .pio/build/meshnology_w12_usbnet/firmware-meshnology_w12_usbnet-*.elf
```

`default_16MB.csv` already carries a 64 KB coredump partition at `0xFF0000`, so no partition-table change is needed.

## Build, flash, test

```bash
# build
pio run -e meshnology_w12_usbnet

# flash - ALWAYS pass --upload-port (see traps)
pio run -e meshnology_w12_usbnet -t upload --upload-port /dev/cu.usbmodem101

# or flash the prebuilt image directly (~20 s, beats the 30 s gadget window)
python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodem101 --baud 921600 \
  write_flash 0x10000 .pio/build/meshnology_w12_usbnet/firmware-*.bin
```

Then plain RST, wait ~30 s for the gadget, and run the gate:

```bash
ifconfig | grep -B5 192.168.7.          # expect 192.168.7.2 on a new enN
ipconfig getpacket enN                  # expect NO router, NO domain_name_server
netstat -rn -f inet | grep default      # must NOT be enN
ping -c3 192.168.7.1
python3 -c "import meshtastic.tcp_interface,time; i=meshtastic.tcp_interface.TCPInterface('192.168.7.1'); time.sleep(3); print(i.getMyNodeInfo()); i.close()"
```

The last line is the one that currently reboots the node.

## Traps that will cost you hours

1. **`pio run -t upload` without `--upload-port` crashes** on a pre-existing repo issue: `AssertionError: Missing target configuration for t-impulse-plus` in the raspberrypi platform's `get_boards()`. Always pass the port.
2. **Do not run `bin/restore-idf-component-yml.sh` before building the usbnet env.** It strips `esp_tinyusb` from the shared framework `idf_component.yml` and the link fails with `cannot find -lespressif__esp_tinyusb`. The platform auto-restores it after a build anyway; the script is belt-and-braces for switching back to other envs.
3. **An existing `sdkconfig.<env>` is authoritative over `custom_sdkconfig`.** Setting `CONFIG_X=n` silently does nothing until you delete that file. This burned two build cycles.
4. **HybridCompile silently reuses cached IDF libs**, so a green build can contain none of your config changes. Symptom: byte-identical flash size and zero relevant symbols. See the force-rebuild recipe above.
5. **The gadget owns the USB pads once it starts**, so the serial console dies and esptool has no RTS line to auto-reset with. `USB_NET_START_DELAY_MS` is 30 s to keep a reflash window. **BOOT+RST parks the chip in ROM download mode with no time limit** - use that, not a race.
6. **esptool's RTS "hard reset" does not reliably boot the app on this board.** Press RST physically after flashing.
7. **W12 emits no plain-text serial log over USB-Serial-JTAG**, even with stock firmware - but it does speak protobuf. `tio /dev/tty.usbmodem101` works and shows the boot log; raw pyserial reads returned 0 bytes.

## Load-bearing design decisions - do not "simplify" these

- **Vend no gateway and no DNS.** `gw = 0.0.0.0`, router offer flag cleared, and `CONFIG_LWIP_DHCPS_ADD_DNS=n`. Apple DTS ([forums 779796](https://developer.apple.com/forums/thread/779796)): an accessory DHCP server must not vend a gateway, or the interface becomes the default route. Without the `ADD_DNS` line, `dhcpserver.c`'s else-branch vends the node's own address as a DNS server that answers nothing. This is what stops iOS electing the link, failing its captive probe, and killing it. **Deliberately not warthog's NAT gateway model.**
- **Bring-up order is the correctness argument.** netif + DHCP + API listener all come up in `setup()`; only then does the USB device start and the NCM link get raised. iOS runs DHCP exactly once on link-up and never retries.
- **`tud_network_default_link_state_cb()` is overridden to start the link DOWN**, for the same reason.
- **TinyUSB must resolve to ≥ 0.21.0** (pinned explicitly in the env). PR #3630 is what makes NCM work on iOS/iPadOS 26; without it DHCP succeeds only ~30% of the time. `esp_tinyusb` alone only requires `>= 0.17.0~2`, so the good resolve was luck.
- **The USB task is pinned to core 0** via `TINYUSB_TASK_CUSTOM(6144, 5, 0)`. esp_tinyusb 2.x moved task config out of Kconfig - `CONFIG_TINYUSB_TASK_STACK_SIZE` and `CONFIG_TINYUSB_TASK_AFFINITY_CPU0` **do not exist** and are silently ignored. Default is core 1 at priority 5, the same core as the Arduino loop task at priority 1.
- **`usbNetTransmit` refuses early and waits only 20 ms.** With `CONFIG_LWIP_TCPIP_CORE_LOCKING=y` it runs inline on the caller under the lwIP core lock, and `tinyusb_net_send_sync` enqueues with an infinite wait. Dropping a frame is free (TCP retransmits); blocking the loop task is not.
- **Never route USB bring-up through `onNetworkConnected()`** - its `displaymode != COLOR` guard would silently kill the feature on exactly the colour-TFT boards someone cables to an iPad.

## Ruled out - don't re-litigate

- **Thread-table exhaustion.** Boot logs `20/40 threads used`. `ServerAPI` is an `OSThread` created per connection and `OSThread`'s ctor does `assert(controller->add(this))`, so this was a real candidate - but there is plenty of headroom.
- **HWCDC console blocking the loop task.** The OLED keeps cycling after the gadget starts, so the loop task is alive. (`Serial.setTxTimeoutMs(0)` was kept anyway; it is defensible on its own merits.)
- **iOS app bug / manual-connect path.** Connecting to `192.168.7.99` does not crash anything, and the reboot reproduces from macOS with no app.
- **Socket layer / `NetworkServer::begin()` failing.** 4403 accepts while 4404 and 80 refuse, so a real listener exists.

## Bugs already found and fixed (all hardware-only, invisible to the build)

1. Zero-initialized `tinyusb_config_t` → task stack size 0, driver refuses. Must use `TINYUSB_DEFAULT_CONFIG()`.
2. Missing `esp_netif_action_start()` / `action_connected()` → netif never comes up, silently answers nothing, host self-assigns 169.254.
3. `action_start` does **not** auto-start dhcps after an explicit `dhcps_stop()` - the explicit fallback is load-bearing (contradicts warthog's comment).
4. `initApiServer()` before any netif exists → null deref on a WiFi-less build (`Not using WIFI` then `LoadProhibited` at `EXCVADDR 0x4c`).
5. The esp_tinyusb Kconfig names above being silently ignored.

## Board eligibility

Needs native USB on the connector (ESP32-S2/S3/P4). C3/C6/H2 have USB-Serial-JTAG only and physically cannot do this. Proxy check: `ARDUINO_USB_CDC_ON_BOOT=1` in the board JSON or variant ini. Definitive: `ioreg -p IOUSB` shows _"Espressif USB JTAG/serial debug unit"_ (native) vs a CP2102/CH9102 bridge. **Heltec V3 is disqualified** - its USB-C is a UART bridge.

## Backlog once the crash is fixed

- Native unit tests for `USBNetPolicy` (it is deliberately free of Arduino/IDF deps so the DHCP-option policy and MAC derivation are testable). Assert options 3 and 6 absent and `gw == 0` - that is the Apple-guidance regression test.
- mDNS on the USB netif. It does **not** auto-join new netifs (predefined set is `WIFI_STA_DEF`/`WIFI_AP_DEF`/`ETH_DEF`); needs `mdns_register_netif()` + `mdns_netif_action(ENABLE_IP4|ANNOUNCE_IP4)`, and there is exactly one free slot at `CONFIG_MDNS_MAX_INTERFACES=3`. Publish TXT `shortname` and `id` or the iOS app renders a blank row.
- Self-powered descriptor at ≤ 50 mA (stock esp_tinyusb descriptor declares bus-powered 100 mA - inside Apple's safe zone, so a refinement not a blocker).
- iPad end-to-end: 20/20 replug lease acquisition, airplane-mode soak, sleep/wake resume.
- Decide whether USB should preempt the single TCP session slot (`ServerAPI.h:43-48`).
