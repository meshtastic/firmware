#pragma once

#if HAS_BLE_MESH && defined(ARCH_NRF52)

#include "mesh/BLEMeshHandler.h"
#include <bluefruit.h>

// Max number of BLE mesh peers we can track
#ifndef BLE_MESH_MAX_PEERS
#define BLE_MESH_MAX_PEERS 8
#endif

// Scan interval and window in units of 0.625ms
// Default: scan 50ms every 500ms (10% duty cycle)
#ifndef BLE_MESH_SCAN_INTERVAL
#define BLE_MESH_SCAN_INTERVAL 800 // 500ms
#endif
#ifndef BLE_MESH_SCAN_WINDOW
#define BLE_MESH_SCAN_WINDOW 80 // 50ms
#endif

// Advertising interval for mesh data in units of 0.625ms
#ifndef BLE_MESH_ADV_INTERVAL
#define BLE_MESH_ADV_INTERVAL 160 // 100ms
#endif

// How many times to repeat a mesh advertisement
#ifndef BLE_MESH_ADV_COUNT
#define BLE_MESH_ADV_COUNT 3
#endif

// Small guard intervals help the SoftDevice finish the stop/start transition
// when we temporarily borrow the single advertising handle.
#ifndef BLE_MESH_ADV_SWITCH_SETTLE_MS
#define BLE_MESH_ADV_SWITCH_SETTLE_MS 2
#endif

#ifndef BLE_MESH_ADV_BURST_GUARD_MS
#define BLE_MESH_ADV_BURST_GUARD_MS 5
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

class NRF52BLEMesh : public BLEMeshHandler
{
  public:
    void start() override;
    void stop() override;
    bool onSend(const meshtastic_MeshPacket *mp) override;
    void onBluetoothReady() override;

  private:
    // Scanning
    void startScanning();
    void stopScanning();
    static void onBleEvent(ble_evt_t *event);
    void handleScanResult(ble_gap_evt_adv_report_t *report);
    bool sendAdvertisementBurst(const uint8_t *advBuf, size_t advLen, PacketId packetId, size_t encodedLen);
    bool shouldUsePriorityRetries(const meshtastic_MeshPacket *mp) const;

    // Peer tracking
    struct BLEMeshPeer {
        NodeNum nodeNum;
        ble_gap_addr_t addr;
        int8_t rssi;
        uint32_t lastSeenMs;
    };
    BLEMeshPeer peers[BLE_MESH_MAX_PEERS];
    uint8_t peerCount = 0;
    void updatePeer(const ble_gap_addr_t &addr, int8_t rssi);
    void pruneStale();

    // Singleton for static callbacks
    static NRF52BLEMesh *instance;
};

#endif // HAS_BLE_MESH && ARCH_NRF52
