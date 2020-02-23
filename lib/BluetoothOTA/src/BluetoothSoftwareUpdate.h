#pragma once

#include <Arduino.h>

BLEService *createUpdateService(BLEServer* server);

void destroyUpdateService();
void bluetoothRebootCheck();