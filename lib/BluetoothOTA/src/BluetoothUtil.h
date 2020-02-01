#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>


// Now handled by BluetoothUtil.cpp
// BLEService *createDeviceInfomationService(BLEServer* server, uint8_t sig, uint16_t vid, uint16_t pid, uint16_t version);

// Help routine to add a description to any BLECharacteristic and add it to the service
void addWithDesc(BLEService *service, BLECharacteristic *c, const char *description);

void dumpCharacteristic(BLECharacteristic *c);

/** converting endianness pull out a 32 bit value */
uint32_t getValue32(BLECharacteristic *c, uint32_t defaultValue);

void loopBLE();
BLEServer *initBLE(std::string devName);