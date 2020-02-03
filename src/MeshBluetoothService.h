#pragma once

#include <BLEService.h>
#include <BLEServer.h>
#include <Arduino.h>

BLEService *createMeshBluetoothService(BLEServer* server);

/**
 * Tell any bluetooth clients that the number of rx packets has changed
 */
void bluetoothNotifyFromNum(uint32_t newValue);
