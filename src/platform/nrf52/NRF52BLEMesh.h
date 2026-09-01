#pragma once

#if HAS_BLE_MESH && defined(ARCH_NRF52)

#include "mesh/BLEMeshHandler.h"
#include <bluefruit.h>

#ifndef BLE_MESH_MAX_PEERS
#define BLE_MESH_MAX_PEERS 8
#endif

// Scan interval and window in units of 0.625ms. Continuous, for the same reason as ESP32.
#ifndef BLE_MESH_SCAN_INTERVAL
#define BLE_MESH_SCAN_INTERVAL 160 // 100ms
#endif
#ifndef BLE_MESH_SCAN_WINDOW
#define BLE_MESH_SCAN_WINDOW 160 // 100ms
#endif

#ifndef BLE_MESH_ADV_INTERVAL
#define BLE_MESH_ADV_INTERVAL 48 // 30ms in units of 0.625ms
#endif

#ifndef BLE_MESH_PEER_TIMEOUT_MS
#define BLE_MESH_PEER_TIMEOUT_MS 300000 // 5 minutes
#endif

class NRF52BLEMesh : public BLEMeshHandler
{
  public:
    void start() override;
    void stop() override;
    void onBluetoothReady() override;

    static void onBleEvent(ble_evt_t *event);

  protected:
    bool platformBeginAdvertising(const uint8_t *adv, size_t len) override;
    bool platformAdvertisingActive() override;
    void platformEndAdvertising() override;
    bool platformReady() override;

  private:
    void startScanning();
    void stopScanning();
    void handleScanResult(ble_gap_evt_adv_report_t *report);

    struct BLEMeshPeer {
        NodeNum nodeNum;
        ble_gap_addr_t addr;
        int8_t rssi;
        uint32_t lastSeenMs;
    };
    BLEMeshPeer peers[BLE_MESH_MAX_PEERS];
    uint8_t peerCount = 0;

    // The SoftDevice advertising set this handler owns. Allocated once by passing
    // BLE_GAP_ADV_SET_HANDLE_NOT_SET, so mesh advertising gets its own set rather than reusing
    // handle 0 - which is Bluefruit's, i.e. the phone's. Reusing it means tearing the phone
    // advertisement down and restoring it around every single frame. If the SoftDevice has no spare
    // set (Bluefruit's default configuration allows one), we fall back to exactly that.
    uint8_t advHandle = BLE_GAP_ADV_SET_HANDLE_NOT_SET;
    bool ownsDedicatedSet = false;
    bool advActive = false;

    // The advertisement payload must stay resident for as long as the SoftDevice is advertising it:
    // sd_ble_gap_adv_set_configure keeps the pointer rather than copying.
    uint8_t advBuf[BLE_MESH_ADV_TOTAL_MAX];
    uint8_t advBufLen = 0;

    void updatePeer(const ble_gap_addr_t &addr, int8_t rssi);
    void pruneStale();

    static NRF52BLEMesh *instance;
};

#endif // HAS_BLE_MESH && ARCH_NRF52
