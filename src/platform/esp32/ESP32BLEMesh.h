#pragma once

#if HAS_BLE_MESH && defined(ARCH_ESP32)

#include "mesh/BLEMeshHandler.h"

// The tree's NimBLE comes from the ESP-IDF component, so the host headers are on the include path
// directly - the same form src/nimble/NimbleBluetooth.cpp uses.
#include "host/ble_gap.h"

// Max number of BLE mesh peers we can track
#ifndef BLE_MESH_MAX_PEERS
#define BLE_MESH_MAX_PEERS 8
#endif

// Scan interval and window in units of 0.625ms.
// Continuous scan (100% duty) to maximise packet capture: unlike LoRa there is no
// retransmit-until-heard, only the fixed BLE_MESH_ADV_EVENTS repeats the sender emits.
#ifndef BLE_MESH_SCAN_INTERVAL
#define BLE_MESH_SCAN_INTERVAL 160 // 100ms
#endif
#ifndef BLE_MESH_SCAN_WINDOW
#define BLE_MESH_SCAN_WINDOW 160 // 100ms
#endif

// Advertising interval for mesh data in units of 0.625ms
#ifndef BLE_MESH_ADV_INTERVAL
#define BLE_MESH_ADV_INTERVAL 48 // 30ms
#endif

// Instance 0 stays with the PhoneAPI's connectable advertisement.
#ifndef BLE_MESH_ADV_INSTANCE
#define BLE_MESH_ADV_INSTANCE 1
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

  protected:
    bool platformBeginAdvertising(const uint8_t *adv, size_t len) override;
    bool platformAdvertisingActive() override;
    void platformEndAdvertising() override;
    bool platformReady() override;

  private:
    // Scanning
    void startScanning();
    void stopScanning();
    static int onGapEvent(struct ble_gap_event *event, void *arg);
    void handleAdvertisement(const struct ble_gap_disc_desc *desc);
#if MYNEWT_VAL(BLE_EXT_ADV)
    void handleExtendedAdvertisement(const struct ble_gap_ext_disc_desc *desc);
    bool configureAdvInstance();
    bool advInstanceConfigured = false;
#endif
    void handleAdvertisementData(const ble_addr_t &addr, int8_t rssi, const uint8_t *data, uint8_t len);

    // Peer tracking
    struct BLEMeshPeer {
        NodeNum nodeNum;
        ble_addr_t addr;
        int8_t rssi;
        uint32_t lastSeenMs;
    };
    BLEMeshPeer peers[BLE_MESH_MAX_PEERS];
    uint8_t peerCount = 0;
    void updatePeer(const ble_addr_t &addr, int8_t rssi);
    void pruneStale();
};

#endif // HAS_BLE_MESH && ARCH_ESP32
