#pragma once

#include <functional>

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include "SimpleAllocator.h"

// Now handled by BluetoothUtil.cpp
// BLEService *createDeviceInfomationService(BLEServer* server, uint8_t sig, uint16_t vid, uint16_t pid, uint16_t version);

// Help routine to add a description to any BLECharacteristic and add it to the service
void addWithDesc(BLEService *service, BLECharacteristic *c, const char *description);

void dumpCharacteristic(BLECharacteristic *c);

/** converting endianness pull out a 32 bit value */
uint32_t getValue32(BLECharacteristic *c, uint32_t defaultValue);

// TODO(girts): create a class for the bluetooth utils helpers?
using StartBluetoothPinScreenCallback = std::function<void(uint32_t pass_key)>;
using StopBluetoothPinScreenCallback = std::function<void(void)>;

void loopBLE();
BLEServer *initBLE(
    StartBluetoothPinScreenCallback startBtPinScreen, StopBluetoothPinScreenCallback stopBtPinScreen,
    std::string devName, std::string hwVendor, std::string swVersion, std::string hwVersion = "");
void deinitBLE();

/// Add a characteristic that we will delete when we restart
BLECharacteristic *addBLECharacteristic(BLECharacteristic *c);

/// Add a characteristic that we will delete when we restart
BLEDescriptor *addBLEDescriptor(BLEDescriptor *c);

/// Given a level between 0-100, update the BLE attribute
void updateBatteryLevel(uint8_t level);

/// Any bluetooth objects you allocate _must_ come from this pool if you want to be able to call deinitBLE()
extern SimpleAllocator btPool;
