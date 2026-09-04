#pragma once
#include "BluetoothCommon.h"

class NimbleBluetooth : BluetoothApi
{
  public:
    void setup();
    void shutdown();
    void deinit();
    void clearBonds();
    bool isActive();
    bool isConnected();
    int getRssi();
    void sendLog(const uint8_t *logMessage, size_t length);
    void startAdvertising();
    bool isDeInit = false;

  private:
    void setupService();
};

void setBluetoothEnable(bool enable);

#if BLE_MESH_USE_EXT_ADV
struct ble_gap_event;
/// The Arduino BLE wrapper's own GAP event handler, for another advertising instance to chain to so the
/// server's connection, MTU and subscription bookkeeping covers the links made through it.
int nimbleServerGapEvent(struct ble_gap_event *event, void *arg);
#endif