#pragma once

#include "BluetoothStatus.h"
#include "GPSStatus.h"
#include "NodeStatus.h"
#include "PowerStatus.h"
#include "detect/ScanI2C.h"
#include "graphics/Screen.h"
#include "memGet.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include <SPI.h>
#include <map>
#if defined(ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S2)
#include "nimble/NimbleBluetooth.h"
extern NimbleBluetooth *nimbleBluetooth;
#endif
#ifdef ARCH_NRF52
#include "NRF52Bluetooth.h"
extern NRF52Bluetooth *nrf52Bluetooth;
#endif
#if !MESHTASTIC_EXCLUDE_I2C
#include "detect/ScanI2CTwoWire.h"
#endif

#if ARCH_PORTDUINO
extern HardwareSPI *DisplaySPI;
extern HardwareSPI *LoraSPI;

#endif
extern ScanI2C::DeviceAddress screen_found;
extern ScanI2C::DeviceAddress cardkb_found;
extern uint8_t kb_model;
extern ScanI2C::DeviceAddress rtc_found;
extern ScanI2C::DeviceAddress accelerometer_found;
extern ScanI2C::FoundDevice rgb_found;

extern bool eink_found;
extern bool pmu_found;
extern bool isCharging;
extern bool isUSBPowered;

#ifdef T_WATCH_S3
#include <Adafruit_DRV2605.h>
extern Adafruit_DRV2605 drv;
#endif

#ifdef HAS_I2S
#include "AudioThread.h"
extern AudioThread *audioThread;
#endif

#ifdef HAS_UDP_MULTICAST
#include "mesh/udp/UdpMulticastHandler.h"
extern UdpMulticastHandler *udpHandler;
#endif

// Global Screen singleton.
extern graphics::Screen *screen;

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C
#include "motion/AccelerometerThread.h"
extern AccelerometerThread *accelerometerThread;
#endif

extern bool isVibrating;

extern int TCPPort; // set by Portduino

// Return a human readable string of the form "Meshtastic_ab13"
const char *getDeviceName();

extern uint32_t timeLastPowered;

extern uint32_t rebootAtMsec;
extern uint32_t shutdownAtMsec;

extern uint32_t serialSinceMsec;

// If a thread does something that might need for it to be rescheduled ASAP it can set this flag
// This will suppress the current delay and instead try to run ASAP.
extern bool runASAP;

extern bool pauseBluetoothLogging;

void nrf52Setup(), esp32Setup(), nrf52Loop(), esp32Loop(), rp2040Setup(), clearBonds(), enterDfuMode();

meshtastic_DeviceMetadata getDeviceMetadata();
#if !MESHTASTIC_EXCLUDE_I2C
void scannerToSensorsMap(const std::unique_ptr<ScanI2CTwoWire> &i2cScanner, ScanI2C::DeviceType deviceType,
                         meshtastic_TelemetrySensorType sensorType);
#endif

// We default to 4MHz SPI, SPI mode 0
extern SPISettings spiSettings;