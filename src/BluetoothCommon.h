#pragma once

#include <Arduino.h>

/**
 * Common lib functions for all platforms that have bluetooth
 */

#define MESH_SERVICE_UUID "6ba1b218-15a8-461f-9fa8-5dcae273eafd"

#define TORADIO_UUID "f75c76d2-129e-4dad-a1dd-7866124401e7"
#define FROMRADIO_UUID "2c55e69e-4993-11ed-b878-0242ac120002"
#define FROMNUM_UUID "ed9da18c-a800-4f66-a670-aa7547e34453"
#define LEGACY_LOGRADIO_UUID "6c6fd238-78fa-436b-aacf-15c5be1ef2e2"
#define LOGRADIO_UUID "5a3d6e49-06e6-4423-9944-e9de8cdf9547"

// NRF52 wants these constants as byte arrays
// Generated here https://yupana-engineering.com/online-uuid-to-c-array-converter - but in REVERSE BYTE ORDER
extern const uint8_t MESH_SERVICE_UUID_16[], TORADIO_UUID_16[16u], FROMRADIO_UUID_16[], FROMNUM_UUID_16[], LOGRADIO_UUID_16[];

/// Given a level between 0-100, update the BLE attribute
void updateBatteryLevel(uint8_t level);

class BluetoothApi
{
  public:
    virtual void setup();
    virtual void shutdown();
    virtual void clearBonds();
    virtual bool isConnected();
    virtual int getRssi() = 0;
};