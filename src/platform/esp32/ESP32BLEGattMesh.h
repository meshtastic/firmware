#pragma once

#if HAS_BLE_GATT_MESH && defined(ARCH_ESP32) && BLE_MESH_USE_EXT_ADV

#include "mesh/BLEGattMeshHandler.h"

#include "host/ble_gap.h"

class BLEServer;

// Instance 0 is the phone API's connectable advertisement and 1 the mesh broadcast. The mesh-peer
// service gets its own connectable set so a phone can find it by service UUID.
#ifndef BLE_GATT_MESH_ADV_INSTANCE
#define BLE_GATT_MESH_ADV_INSTANCE 2
#endif

// Advertising interval in 0.625 ms units; slower than the phone API's, it only has to be found.
#ifndef BLE_GATT_MESH_ADV_ITVL_MIN
#define BLE_GATT_MESH_ADV_ITVL_MIN 0x50 // 50 ms
#endif
#ifndef BLE_GATT_MESH_ADV_ITVL_MAX
#define BLE_GATT_MESH_ADV_ITVL_MAX 0xA0 // 100 ms
#endif

// Links accepted through the mesh-peer advertisement. CONFIG_BT_NIMBLE_MAX_CONNECTIONS is 2 and one
// slot stays reserved for the phone API, so the advertisement only runs while there is room.
#ifndef BLE_GATT_MESH_MAX_LINKS
#define BLE_GATT_MESH_MAX_LINKS 1
#endif

// Received writes waiting for the main task; each holds one ATT value, so up to 512 bytes.
#ifndef BLE_GATT_MESH_RX_QUEUE_SIZE
#define BLE_GATT_MESH_RX_QUEUE_SIZE 6
#endif

class ESP32BLEGattMesh : public BLEGattMeshHandler
{
  public:
    void start() override;
    void stop() override;

    /// Register the mesh-peer service. Called from NimbleBluetooth::setupService() on the main task.
    static void setupService(BLEServer *server);
    /// Start (or restart) the connectable mesh-peer advertisement. Main task only.
    static void startAdvertising();
    /// A central connected (any advertising instance). Register it as a candidate mesh link so its
    /// subscribe/notify state is tracked no matter which advertisement it arrived on. Fires from the
    /// server's own connect callback, which - unlike a per-instance GAP callback - sees every link.
    static void onConnect(uint16_t connHandle);
    /// A link dropped: forget it. True when it came in through the mesh-peer advertisement, so the
    /// phone API's own session handling must not run for it.
    static bool onDisconnect(uint16_t connHandle);
    /// The server is being torn down and its characteristics freed.
    static void teardown();

  protected:
    bool platformReady() override;
    size_t platformPeers(BLEGattMeshPeer *out, size_t cap) override;
    bool platformNotify(BLEGattPeerId peer, const uint8_t *data, size_t len) override;
    bool platformPollInbound(BLEGattPeerId &peer, uint8_t *buf, size_t cap, size_t &len) override;
    int32_t runOnce() override;

  private:
    static int onGapEvent(struct ble_gap_event *event, void *arg);
};

#endif // HAS_BLE_GATT_MESH && ARCH_ESP32 && BLE_MESH_USE_EXT_ADV
