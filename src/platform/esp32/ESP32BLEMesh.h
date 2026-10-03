#pragma once

#if HAS_BLE_MESH && defined(ARCH_ESP32)

#include "mesh/BLEMeshHandler.h"

// NimBLE comes from the ESP-IDF component, so the host headers are on the include path directly.
#include "host/ble_gap.h"

// Units of 0.625ms. Window equals interval: a sender emits only BLE_MESH_ADV_EVENTS repeats and
// nothing retransmits until heard, so a gap in the duty cycle can only lose frames.
#ifndef BLE_MESH_SCAN_INTERVAL
#define BLE_MESH_SCAN_INTERVAL 160 // 100ms
#endif
#ifndef BLE_MESH_SCAN_WINDOW
#define BLE_MESH_SCAN_WINDOW 160 // 100ms
#endif

// Units of 0.625ms.
#ifndef BLE_MESH_ADV_INTERVAL
#define BLE_MESH_ADV_INTERVAL 48 // 30ms
#endif

// Instance 0 stays with the PhoneAPI's connectable advertisement.
#ifndef BLE_MESH_ADV_INSTANCE
#define BLE_MESH_ADV_INSTANCE 1
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
#if BLE_MESH_USE_EXT_ADV
    void handleExtendedAdvertisement(const struct ble_gap_ext_disc_desc *desc);
    bool configureAdvInstance();
    bool advInstanceConfigured = false;
#endif
    void handleAdvertisementData(int8_t rssi, const uint8_t *data, uint8_t len);
};

#endif // HAS_BLE_MESH && ARCH_ESP32
