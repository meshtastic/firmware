#pragma once

#if HAS_BLE_GATT_MESH && defined(ARCH_ESP32) && !MESHTASTIC_EXCLUDE_BLUETOOTH

#include "mesh/BLEGattMeshHandler.h"

class BLEServer;
class BLEAdvertisementData;

// Peripheral links the host holds: the phone and one mesh peer. Must equal the env's
// CONFIG_BT_NIMBLE_MAX_CONNECTIONS, which is read here as a number rather than from sdkconfig.h
// because the prebuilt headers need not match the rebuilt libraries.
#ifndef BLE_GATT_MESH_ESP32_LINKS
#define BLE_GATT_MESH_ESP32_LINKS 2
#endif

// Received writes waiting for the main task; each holds one ATT value, so up to 512 bytes.
#ifndef BLE_GATT_MESH_RX_QUEUE_SIZE
#define BLE_GATT_MESH_RX_QUEUE_SIZE 6
#endif

/**
 * The mesh-peer GATT service on NimBLE. One legacy connectable advertisement serves the phone and the
 * mesh peers alike, with the service UUID in its scan response; a link becomes a mesh peer when it
 * subscribes to the mesh characteristic. The NimBLE host task runs the callbacks and the pump runs on
 * the main task, hence the lock.
 */
class ESP32BLEGattMesh : public BLEGattMeshHandler
{
  public:
    void start() override;
    void stop() override;

    /// True when the GATT peer protocol is enabled; every hook below is inert otherwise.
    static bool enabled();
    /// Register the service. Called from NimbleBluetooth::setupService() before the first advertisement,
    /// whose start resolves the characteristic's value handle.
    static void setupService(BLEServer *server);
    /// Put the service UUID and a name that fits beside it in the scan response.
    static void fillScanResponse(BLEAdvertisementData &scan, const char *name);
    /// Every inbound link, from the server's connect and disconnect callbacks. onDisconnect returns
    /// true when the link had subscribed to the mesh characteristic.
    static void onConnect(uint16_t conn);
    static bool onDisconnect(uint16_t conn);
    /// True while a connection slot is free for the advertisement to fill.
    static bool hasFreeLink();
    /// Copy the handles of every live link into out; returns the count. For teardown.
    static size_t linkHandles(uint16_t *out, size_t cap);
    /// The server is being torn down and its characteristics freed.
    static void teardown();

  protected:
    bool platformReady() override;
    size_t platformPeers(BLEGattMeshPeer *out, size_t cap) override;
    bool platformNotify(BLEGattPeerId peer, const uint8_t *data, size_t len) override;
    bool platformPollInbound(BLEGattPeerId &peer, uint8_t *buf, size_t cap, size_t &len) override;
};

#endif // HAS_BLE_GATT_MESH && ARCH_ESP32 && !MESHTASTIC_EXCLUDE_BLUETOOTH
