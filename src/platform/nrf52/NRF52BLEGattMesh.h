#pragma once

#if HAS_BLE_GATT_MESH && defined(ARCH_NRF52)

#include "mesh/BLEGattMeshHandler.h"

// Received writes waiting for the main task; each holds one ATT value, so up to 512 bytes.
#ifndef BLE_GATT_MESH_RX_QUEUE_SIZE
#define BLE_GATT_MESH_RX_QUEUE_SIZE 6
#endif

/**
 * The mesh-peer GATT service on the SoftDevice. S140 has one advertising set, so the service UUID
 * rides in the phone advertisement's scan response and a mesh peer connects to the same set the
 * phone does; a link becomes a mesh peer when it subscribes to the mesh characteristic. Bluefruit
 * runs the callbacks on its own task and the pump runs on the main task, hence the lock.
 */
class NRF52BLEGattMesh : public BLEGattMeshHandler
{
  public:
    void start() override;
    void stop() override;
    /// Register the service. Called from setupMeshService() after the phone-API service.
    static void setupService();
    /// Put the service UUID in the scan response. True when it was added (and the name will be
    /// shortened to fit beside it); false when the GATT peer protocol is off.
    static bool addToScanResponse();
    /// Bluefruit connect/disconnect, for every peripheral link. onDisconnect returns true when the
    /// link had subscribed to the mesh characteristic: the phone API's session handling must not run.
    static void onConnect(uint16_t conn);
    static bool onDisconnect(uint16_t conn);

  protected:
    bool platformReady() override;
    size_t platformPeers(BLEGattMeshPeer *out, size_t cap) override;
    bool platformNotify(BLEGattPeerId peer, const uint8_t *data, size_t len) override;
    bool platformPollInbound(BLEGattPeerId &peer, uint8_t *buf, size_t cap, size_t &len) override;
};

#endif // HAS_BLE_GATT_MESH && ARCH_NRF52
