#pragma once

#include <Arduino.h>

BLEService *createUpdateService(BLEServer* server);

void bluetoothRebootCheck();