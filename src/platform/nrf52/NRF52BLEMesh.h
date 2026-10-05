#pragma once

#if HAS_BLE_MESH && defined(ARCH_NRF52)

#include "mesh/BLEMeshHandler.h"
#include <bluefruit.h>

// Units of 0.625ms. Window equals interval: continuous, as on ESP32.
#ifndef BLE_MESH_SCAN_INTERVAL
#define BLE_MESH_SCAN_INTERVAL 160 // 100ms
#endif
#ifndef BLE_MESH_SCAN_WINDOW
#define BLE_MESH_SCAN_WINDOW 160 // 100ms
#endif

#ifndef BLE_MESH_ADV_INTERVAL
#define BLE_MESH_ADV_INTERVAL 48 // 30ms in units of 0.625ms
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

    // A SoftDevice advertising set of its own, allocated by passing BLE_GAP_ADV_SET_HANDLE_NOT_SET.
    // Handle 0 is Bluefruit's phone advertisement; sharing it means tearing that down and restoring
    // it around every frame, which is the fallback when the SoftDevice has no spare set.
    uint8_t advHandle = BLE_GAP_ADV_SET_HANDLE_NOT_SET;
    bool ownsDedicatedSet = false;
    bool advActive = false;

    // The advertisement payload must stay resident for as long as the SoftDevice is advertising it:
    // sd_ble_gap_adv_set_configure keeps the pointer rather than copying.
    uint8_t advBuf[BLE_MESH_ADV_TOTAL_MAX];
    uint8_t advBufLen = 0;

    static NRF52BLEMesh *instance;
};

#endif // HAS_BLE_MESH && ARCH_NRF52
