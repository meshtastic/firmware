#pragma once

#include <BLEService.h>
#include <BLEServer.h>
#include <Arduino.h>

BLEService *createMeshBluetoothService(BLEServer* server);
void destroyMeshBluetoothService();

/**
 * Tell any bluetooth clients that the number of rx packets has changed
 */
void bluetoothNotifyFromNum(uint32_t newValue);

/**
 * Super skanky FIXME - when we start a software update we force the mesh service to shutdown.
 * If the sw update fails, the user will have to manually reset the board to get things running again.
 */
void stopMeshBluetoothService();