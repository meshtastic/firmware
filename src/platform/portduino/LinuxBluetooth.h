#pragma once

#include "configuration.h"

// BLE peripheral support for meshtasticd, implemented against BlueZ's D-Bus
// GATT, advertising and agent APIs via sdbus-c++. Compiled only on Linux hosts
// with the sdbus-c++ dev package present (same optional-dependency idiom as the
// ulfius webserver); everywhere else this header is empty and HAS_BLUETOOTH
// stays 0.
#if defined(ARCH_PORTDUINO) && defined(PORTDUINO_LINUX_HARDWARE) && __has_include(<sdbus-c++/sdbus-c++.h>)

#define MESHTASTIC_LINUX_BLE 1

#include <cstddef>
#include <cstdint>
#include <memory>

/**
 * Runs the standard Meshtastic BLE service (toRadio/fromRadio/fromNum/logRadio)
 * through bluetoothd. Method names deliberately match
 * NimbleBluetooth/NRF52Bluetooth so the cross-platform call sites
 * (setBluetoothEnable, AdminModule, RedirectablePrint, ...) read the same on
 * every architecture.
 *
 * Note this intentionally does not derive from BluetoothApi: no call site uses
 * that base polymorphically (each platform is reached through its own concrete
 * global pointer), and the base's declared-but-undefined virtuals make its
 * vtable/typeinfo unlinkable in unoptimized RTTI builds such as the native
 * coverage env.
 */
class LinuxBluetooth
{
  public:
    LinuxBluetooth();
    ~LinuxBluetooth();

    /// Connect to BlueZ, export the GATT application, register agent +
    /// advertisement. Failure (no adapter, D-Bus policy denial, no bluetoothd)
    /// logs and leaves BLE off.
    void setup();
    /// Stop advertising only; an established connection stays up (PowerFSM dark
    /// states).
    void shutdown();
    /// Re-register the advertisement after shutdown().
    void resumeAdvertising();
    /// Full teardown: unregister everything and drop the bus connection.
    void deinit();

    void clearBonds();
    bool isConnected();
    int getRssi();
    /// setup() succeeded and deinit() has not run.
    bool isEnabled();
    void sendLog(const uint8_t *logMessage, size_t length);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif
