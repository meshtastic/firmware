#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

BLEService *createUpdateService(BLEServer *server, std::string hwVendor, std::string swVersion, std::string hwVersion);

void destroyUpdateService();
void bluetoothRebootCheck();