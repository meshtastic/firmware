# WireGuard VPN Developer Guide

This document explains how the experimental WireGuard VPN support in the Meshtastic firmware is implemented and how developers can interact with it.

## Enabling the Feature

WireGuard functionality is optional and controlled by the `HAS_WIREGUARD_VPN` compile-time flag. By default this flag is disabled in `src/configuration.h`.

To build a variant with VPN support you must:

1. Include the `ciniml/WireGuard-ESP32` library in your `platformio.ini` variant.
2. Add `-DHAS_WIREGUARD_VPN=1` to the variant's `build_flags`.

See the below `seeed_xiao_s3` variant for an example configuration

```cpp
    [env:seeed-xiao-s3]
    extends = esp32s3_base
    board = seeed-xiao-s3
    board_check = true
    board_build.partitions = default_8MB.csv
    upload_protocol = esptool
    upload_speed = 921600
    upload_port = COM18
    lib_deps =
        ${esp32s3_base.lib_deps}
        ciniml/WireGuard-ESP32@^0.1.5
    build_unflags =
        ${esp32s3_base.build_unflags}
        -DARDUINO_USB_MODE=1
    build_flags = 
        ${esp32s3_base.build_flags}
        -D SEEED_XIAO_S3
        -I variants/esp32s3/seeed_xiao_s3
        -DBOARD_HAS_PSRAM 
        -DHAS_WIREGUARD_VPN=1
        -DARDUINO_USB_MODE=0
```


## Configuration Structure

The runtime configuration is defined in `src/mesh/wireguard/WireGuardConfig.h`:

```cpp
typedef struct WireGuardConfig {
    bool enabled;           ///< Whether the tunnel should start when networking and NTP are ready
    char address[32];       ///< Client IPv4 address (e.g. 10.0.0.2)
    char serverAddr[64];    ///< WireGuard server host
    uint16_t serverPort;       ///< WireGuard server port
    char privateKey[64];    ///< Client private key
    char publicKey[64];     ///< Server public key
    char presharedKey[64];  ///< Optional preshared key
} WireGuardConfig;
```

Default values for these fields can be set at compile time using the `WIREGUARD_DEFAULT_*` macros in the same header, but production firmware should leave them blank and disabled. A global instance `wireGuardConfig` is allocated in `WireGuardConfig.cpp` and is synchronized from `moduleConfig.wireguard` after loading, restoring, or receiving admin updates.

The protobuf definition `meshtastic_ModuleConfig_WireGuardConfig` (generated in `module_config.pb.h`) mirrors this structure so that the values can be updated over the admin API. It also reports transient runtime `status` and `last_error` fields in get responses.

## Device configuration

Use `bin/wireguard-config.py` with a meshtastic-python build that includes this branch's protobufs:

```bash
python bin/wireguard-config.py --port COM7 set \
  --enable \
  --address 10.0.0.2 \
  --server-addr wg.example.net \
  --server-port 51820 \
  --private-key CLIENT_PRIVATE_KEY \
  --public-key SERVER_PUBLIC_KEY
```

Read config and runtime status:

```bash
python bin/wireguard-config.py --port COM7 get
```

Disable startup without erasing keys:

```bash
python bin/wireguard-config.py --port COM7 disable
```

Private and preshared keys are redacted from script output unless `--show-secrets` is passed. When updating other fields, pass `sekrit` as the private or preshared key to preserve the existing value through the firmware admin handler.

## WireGuard API

The public API is declared in `src/mesh/wireguard/WireGuardVPN.h`:

```cpp
bool startWireGuard();    // Start the VPN tunnel
void stopWireGuard();     // Stop the VPN service
bool isWireGuardRunning();
```

`startWireGuard()` attempts to create a tunnel only when WireGuard is enabled and all required fields are configured. It first checks that the device has valid NTP time and an active network (Wi‑Fi or Ethernet). On success the global VPN instance becomes active and `isWireGuardRunning()` returns `true`.

`stopWireGuard()` tears down the tunnel if it is running.

## Automatic Control

The Wi‑Fi and Ethernet client code automatically manages the VPN. When a network connection is established and the real‑time clock is synced, `startWireGuard()` is called. When the connection drops, `stopWireGuard()` is invoked. This logic can be seen in `WiFiAPClient.cpp` and `ethClient.cpp`.

## Interacting from Other Modules

Other modules may start or stop the VPN by calling the API functions above. They can also examine or modify `wireGuardConfig` before starting the tunnel. For remote configuration, use the admin protobuf message `AdminMessage_ModuleConfigType_WIREGUARD_CONFIG`.

Because this feature is experimental the implementation may evolve, but these entry points are expected to remain stable.
