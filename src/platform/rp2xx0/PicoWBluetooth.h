#pragma once
// Bluetooth LE transport for the Pico W / Pico 2 W (CYW43439 + BTstack from
// arduino-pico).
//
// Threading model: BTstack runs on the core's lwIP FreeRTOS task. Every BTstack
// call we make is marshalled onto that task with lwip_callback(); the ATT
// read/write callbacks run there too and only touch lock-free buffers. The
// Meshtastic PhoneAPI is driven from the main task by a small OSThread that
// drains those buffers.
#include <stdint.h>

class PicoWBluetooth
{
  public:
    void setup();            // one-time BTstack bring-up (idempotent)
    void startAdvertising(); // (re)enable advertising
    void shutdown();         // stop advertising and drop any client
    bool isConnected();
    void clearBonds();
    int countBonds();
    void setBatteryLevel(uint8_t level);
};

extern PicoWBluetooth *picoWBluetooth;

// Bond store (BTstack TLV) kept in RAM and persisted to LittleFS; called from
// the main task.
void picoWBondStoreFlush();
void picoWBondStoreLoad();      // main task, before the controller is powered on
void picoWBondStoreAttachTlv(); // Bluetooth task, right after that
