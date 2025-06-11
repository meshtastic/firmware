# WireGuard VPN Developer Guide

This document explains how the experimental WireGuard VPN support in the Meshtastic firmware is implemented and how developers can interact with it.

## Enabling the Feature

WireGuard functionality is optional and controlled by the `HAS_WIREGUARD_VPN` compile-time flag. By default this flag is disabled in `src/configuration.h`.

To build a variant with VPN support you must:

1. Include the `Wireguard-ESP32` library in your `platformio.ini` variant.
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
        Wireguard-ESP32
    build_unflags =
        ${esp32s3_base.build_unflags}
        -DARDUINO_USB_MODE=1
    build_flags = 
        ${esp32s3_base.build_flags} -DSEEED_XIAO_S3 -I variants/seeed_xiao_s3
        -DBOARD_HAS_PSRAM 

        -DHAS_WIREGUARD_VPN=1

        -DARDUINO_USB_MODE=0
```


## Configuration Structure

The runtime configuration is defined in `src/mesh/wireguard/WireGuardConfig.h`:

```cpp
typedef struct WireGuardConfig {
    const char *address;       ///< Client IPv4 address (e.g. 10.0.0.2)
    const char *serverAddr;    ///< WireGuard server host
    uint16_t serverPort;       ///< WireGuard server port
    const char *privateKey;    ///< Client private key
    const char *publicKey;     ///< Server public key
    const char *presharedKey;  ///< Optional preshared key
} WireGuardConfig;
```

Default values for these fields can be set at compile time using the `WIREGUARD_DEFAULT_*` macros in the same header. A global instance `wireGuardConfig` is allocated in `WireGuardConfig.cpp` and can be modified at runtime.

The protobuf definition `meshtastic_ModuleConfig_WireGuardConfig` (generated in `module_config.pb.h`) mirrors this structure so that the values can be updated over the admin API.

## WireGuard API

The public API is declared in `src/mesh/wireguard/WireGuardVPN.h`:

```cpp
bool startWireGuard();    // Start the VPN tunnel
void stopWireGuard();     // Stop the VPN service
bool isWireGuardRunning();
```

`startWireGuard()` attempts to create a tunnel using the values in `wireGuardConfig`. It first checks that the device has valid NTP time and an active network (Wi‑Fi or Ethernet). On success the global VPN instance becomes active and `isWireGuardRunning()` returns `true`.

`stopWireGuard()` tears down the tunnel if it is running.

## Automatic Control

The Wi‑Fi and Ethernet client code automatically manages the VPN. When a network connection is established and the real‑time clock is synced, `startWireGuard()` is called. When the connection drops, `stopWireGuard()` is invoked. This logic can be seen in `WiFiAPClient.cpp` and `ethClient.cpp`.

## Interacting from Other Modules

Other modules may start or stop the VPN by calling the API functions above. They can also examine or modify `wireGuardConfig` before starting the tunnel. For remote configuration, use the admin protobuf message `AdminMessage_ModuleConfigType_WIREGUARD_CONFIG`.

Because this feature is experimental the implementation may evolve, but these entry points are expected to remain stable.
