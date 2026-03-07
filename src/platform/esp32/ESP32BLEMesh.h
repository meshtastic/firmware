#pragma once

#if HAS_BLE_MESH && defined(ARCH_ESP32)

#include "mesh/BLEMeshHandler.h"

#if defined(CONFIG_NIMBLE_CPP_IDF)
#include "host/ble_gap.h"
#else
#include "nimble/nimble/host/include/host/ble_gap.h"
#endif

// Max number of BLE mesh peers we can track
#ifndef BLE_MESH_MAX_PEERS
#define BLE_MESH_MAX_PEERS 8
#endif

// Scan interval and window in units of 0.625ms
// Default: continuous scan (100% duty) to maximize packet capture reliability.
// For lower power builds, these can be overridden in build flags.
#ifndef BLE_MESH_SCAN_INTERVAL
#define BLE_MESH_SCAN_INTERVAL 160 // 100ms
#endif
#ifndef BLE_MESH_SCAN_WINDOW
#define BLE_MESH_SCAN_WINDOW 160 // 100ms
#endif

// Advertising interval for mesh data in units of 0.625ms
#ifndef BLE_MESH_ADV_INTERVAL
#define BLE_MESH_ADV_INTERVAL 160 // 100ms
#endif

// How many times to repeat a mesh advertisement
#ifndef BLE_MESH_ADV_COUNT
#define BLE_MESH_ADV_COUNT 3
#endif

#ifndef BLE_MESH_ADV_BURST_MS
#define BLE_MESH_ADV_BURST_MS (BLE_MESH_ADV_COUNT * 100)
#endif

// Extra retry bursts for higher-priority traffic only.
// Base burst always runs once for every packet.
#ifndef BLE_MESH_PRIORITY_EXTRA_RETRIES
#define BLE_MESH_PRIORITY_EXTRA_RETRIES 2
#endif

// Randomized delay range between extra retry bursts in milliseconds.
#ifndef BLE_MESH_PRIORITY_RETRY_JITTER_MIN_MS
#define BLE_MESH_PRIORITY_RETRY_JITTER_MIN_MS 50
#endif
#ifndef BLE_MESH_PRIORITY_RETRY_JITTER_MAX_MS
#define BLE_MESH_PRIORITY_RETRY_JITTER_MAX_MS 200
#endif

// How long before a peer is considered stale (ms)
#ifndef BLE_MESH_PEER_TIMEOUT_MS
#define BLE_MESH_PEER_TIMEOUT_MS 300000 // 5 minutes
#endif

class ESP32BLEMesh : public BLEMeshHandler
{
  public:
    void start() override;
    void stop() override;
    void onBluetoothReady() override;
    bool onSend(const meshtastic_MeshPacket *mp) override;

  private:
    // Scanning
    void startScanning();
    void stopScanning();
    static int onGapEvent(struct ble_gap_event *event, void *arg);
    void handleAdvertisement(const struct ble_gap_disc_desc *desc);
#if MYNEWT_VAL(BLE_EXT_ADV)
    void handleExtendedAdvertisement(const struct ble_gap_ext_disc_desc *desc);
#endif
    void handleAdvertisementData(const ble_addr_t &addr, int8_t rssi, const uint8_t *data, uint8_t len);
    bool sendAdvertisementBurst(const uint8_t *advBuf, size_t advLen, PacketId packetId, size_t encodedLen);
    bool shouldUsePriorityRetries(const meshtastic_MeshPacket *mp) const;

    // Peer tracking
    struct BLEMeshPeer {
        NodeNum nodeNum;
        ble_addr_t addr;
        int8_t rssi;
        uint32_t lastSeenMs;
    };
    BLEMeshPeer peers[BLE_MESH_MAX_PEERS];
    uint8_t peerCount = 0;
    bool bluetoothReady = false;
    void updatePeer(const ble_addr_t &addr, int8_t rssi);
    void pruneStale();
};

#endif // HAS_BLE_MESH && ARCH_ESP32
