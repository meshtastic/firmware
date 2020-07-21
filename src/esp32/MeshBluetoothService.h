#pragma once

#include <Arduino.h>
#include <BLEServer.h>
#include <BLEService.h>

#ifdef CONFIG_BLUEDROID_ENABLED

BLEService *createMeshBluetoothService(BLEServer *server);

#endif

void destroyMeshBluetoothService();
void stopMeshBluetoothService();