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
extern bool kb_found;
extern bool osk_found;
extern ScanI2C::DeviceAddress rtc_found;
extern ScanI2C::DeviceAddress accelerometer_found;
extern ScanI2C::FoundDevice rgb_found;
extern ScanI2C::DeviceAddress aqi_found;

extern bool eink_found;
extern bool pmu_found;
extern bool isUSBPowered;

#if defined(T_WATCH_S3) || defined(T_LORA_PAGER)
#include <Adafruit_DRV2605.h>
extern Adafruit_DRV2605 drv;
#endif

#ifdef HAS_I2S
#include "AudioThread.h"
extern AudioThread *audioThread;
#endif

#ifdef ELECROW_ThinkNode_M5
#include <PCA9557.h>
extern PCA9557 io;
#endif

#ifdef HAS_UDP_MULTICAST
#include "mesh/udp/UdpMulticastHandler.h"
extern UdpMulticastHandler *udpHandler;
#endif

// Global Screen singleton.
extern graphics::Screen *screen;

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C
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

#if defined(ELECROW_ThinkNode_M4)
#include <SoftwareSerial.h>
#include "nRF52_PWM.h"

typedef enum
{
    DATA_LED_IDLE = 1,
    DATA_LED_ON,
    DATA_LED_OFF,
    DATA_LORA_TX_BLINK,
    DATA_LORA_RX_BLINK,
    DATA_GPS_BREATHE,
    DATA_GPS_BLINK,
} DATA_LED_State;

typedef enum
{
    PWR_LED_IDLE = 1,
    PWR_BLE_PAIRING,
    PWR_BLE_CONNECTD,
    PWR_BLE_DISCONNECTD,
} PWR_LED_State;

typedef enum
{
    BAT_LED_IDLE = 1,
    BAT_LED_LOW,
    BAT_LED_MIDDLE_LOW,
    BAT_LED_MIDDLE_HIGH,
    BAT_LED_HIGH,
} BAT_LED_State;

typedef enum
{
    BAT_LOW = 1,
    BAT_MIDDLE_LOW,
    BAT_MIDDLE_HIGH,
    BAT_HIGH,
} BAT_State;

typedef enum
{
    GPS_DISENABLE = 1,
    GPS_SEEK,
    GPS_LOCATING,
} User_GPS_State;

typedef enum
{
    BATTERY_IDLE = 1,
    BATTERY_WORK,
} User_Battery_State;

typedef struct
{
    float voltage;
    uint32_t soc_voltage;
} Battery_value_t;

void set_off_status(bool state);
bool get_off_status();

void set_txstatus(bool state);
void set_tx_id(uint32_t id);
uint32_t get_tx_id();
void set_rxstatus(bool state);
void set_rx_id(uint32_t id);
uint32_t get_rx_id();

bool get_ble_flag();

bool get_periphstatus(void);
void SetperiphMode(bool state);
void set_gps_connect_status(User_GPS_State state);
void set_gps_state(bool state);

void set_data_led_state(DATA_LED_State state);
void set_data_led_status(bool state);
void DATA_LED_status(void);

void set_pwr_led_state(PWR_LED_State state);
void FAST_BLINK_STOP(void);
void PWE_LED_status(void);

void User_Battery_state(void);
void Battery_LED_status(void);
void set_battery_state(BAT_LED_State state);
BAT_LED_State get_last_BAT_LED_state();

void set_battery_flag(bool state);
bool get_battery_flag();
void set_bat_last_time(uint32_t time);
uint32_t get_battery_soc();
float get_battery_voltage();
bool get_charge_state();
#endif

// We default to 4MHz SPI, SPI mode 0
extern SPISettings spiSettings;
