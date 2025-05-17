#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "PowerMon.h"
#include "ReliableRouter.h"
#include "airtime.h"
#include "buzz.h"

#include "FSCommon.h"
#include "Led.h"
#include "RTC.h"
#include "SPILock.h"
#include "Throttle.h"
#include "concurrency/OSThread.h"
#include "concurrency/Periodic.h"
#include "detect/ScanI2C.h"
#include "error.h"
#include "power.h"

#if !MESHTASTIC_EXCLUDE_I2C
#include "detect/ScanI2CTwoWire.h"
#include <Wire.h>
#endif
#include "detect/einkScan.h"
#include "graphics/RAKled.h"
#include "graphics/Screen.h"
#include "main.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include "meshUtils.h"
#include "modules/Modules.h"
#include "shutdown.h"
#include "sleep.h"
#include "target_specific.h"
#include <memory>
#include <utility>

#ifdef ARCH_ESP32
#include "freertosinc.h"
#if !MESHTASTIC_EXCLUDE_WEBSERVER
#include "mesh/http/WebServer.h"
#endif
#if !MESHTASTIC_EXCLUDE_BLUETOOTH
#include "nimble/NimbleBluetooth.h"
NimbleBluetooth *nimbleBluetooth = nullptr;
#endif
#endif

#ifdef ARCH_NRF52
#include "NRF52Bluetooth.h"
NRF52Bluetooth *nrf52Bluetooth = nullptr;
#endif

#if HAS_WIFI || defined(USE_WS5500)
#include "mesh/api/WiFiServerAPI.h"
#include "mesh/wifi/WiFiAPClient.h"
#endif

#if HAS_ETHERNET && !defined(USE_WS5500)
#include "mesh/api/ethServerAPI.h"
#include "mesh/eth/ethClient.h"
#endif

#if !MESHTASTIC_EXCLUDE_MQTT
#include "mqtt/MQTT.h"
#endif

#include "LLCC68Interface.h"
#include "LR1110Interface.h"
#include "LR1120Interface.h"
#include "LR1121Interface.h"
#include "RF95Interface.h"
#include "SX1262Interface.h"
#include "SX1268Interface.h"
#include "SX1280Interface.h"
#include "detect/LoRaRadioType.h"

#ifdef ARCH_STM32WL
#include "STM32WLE5JCInterface.h"
#endif

#if defined(ARCH_PORTDUINO)
#include "platform/portduino/SimRadio.h"
#endif

#ifdef ARCH_PORTDUINO
#include "linux/LinuxHardwareI2C.h"
#include "mesh/raspihttp/PiWebServer.h"
#include "platform/portduino/PortduinoGlue.h"
#include "platform/portduino/USBHal.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#endif

#if HAS_BUTTON || defined(ARCH_PORTDUINO)
#include "ButtonThread.h"
#endif

#include "AmbientLightingThread.h"
#include "PowerFSMThread.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C
#include "motion/AccelerometerThread.h"
AccelerometerThread *accelerometerThread = nullptr;
#endif

#ifdef HAS_I2S
#include "AudioThread.h"
AudioThread *audioThread = nullptr;
#endif

#ifdef USE_PCA9557
PCA9557 IOEXP;
#endif

#if HAS_TFT
extern void tftSetup(void);
#endif

#ifdef HAS_UDP_MULTICAST
#include "mesh/udp/UdpMulticastHandler.h"
UdpMulticastHandler *udpHandler = nullptr;
#endif

#if defined(TCXO_OPTIONAL)
float tcxoVoltage = SX126X_DIO3_TCXO_VOLTAGE; // if TCXO is optional, put this here so it can be changed further down.
#endif

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
void setupNicheGraphics();
#include "nicheGraphics.h"
#endif

#if defined(HW_SPI1_DEVICE) && defined(ARCH_ESP32)
SPIClass SPI1(HSPI);
#endif

using namespace concurrency;

volatile static const char slipstreamTZString[] = {USERPREFS_TZ_STRING};

// We always create a screen object, but we only init it if we find the hardware
graphics::Screen *screen = nullptr;

// Global power status
meshtastic::PowerStatus *powerStatus = new meshtastic::PowerStatus();

// Global GPS status
meshtastic::GPSStatus *gpsStatus = new meshtastic::GPSStatus();

// Global Node status
meshtastic::NodeStatus *nodeStatus = new meshtastic::NodeStatus();

// Global Bluetooth status
meshtastic::BluetoothStatus *bluetoothStatus = new meshtastic::BluetoothStatus();

// Scan for I2C Devices

/// The I2C address of our display (if found)
ScanI2C::DeviceAddress screen_found = ScanI2C::ADDRESS_NONE;

// The I2C address of the cardkb or RAK14004 (if found)
ScanI2C::DeviceAddress cardkb_found = ScanI2C::ADDRESS_NONE;
// 0x02 for RAK14004, 0x00 for cardkb, 0x10 for T-Deck
uint8_t kb_model;

// The I2C address of the RTC Module (if found)
ScanI2C::DeviceAddress rtc_found = ScanI2C::ADDRESS_NONE;
// The I2C address of the Accelerometer (if found)
ScanI2C::DeviceAddress accelerometer_found = ScanI2C::ADDRESS_NONE;
// The I2C address of the RGB LED (if found)
ScanI2C::FoundDevice rgb_found = ScanI2C::FoundDevice(ScanI2C::DeviceType::NONE, ScanI2C::ADDRESS_NONE);

#ifdef T_WATCH_S3
Adafruit_DRV2605 drv;
#endif

// Global LoRa radio type
LoRaRadioType radioType = NO_RADIO;

bool isVibrating = false;

bool eink_found = true;

uint32_t serialSinceMsec;
bool pauseBluetoothLogging = false;

bool pmu_found;

#if !MESHTASTIC_EXCLUDE_I2C
// Array map of sensor types with i2c address and wire as we'll find in the i2c scan
std::pair<uint8_t, TwoWire *> nodeTelemetrySensorsMap[_meshtastic_TelemetrySensorType_MAX + 1] = {};
#endif

Router *router = NULL; // Users of router don't care what sort of subclass implements that API

const char *firmware_version = optstr(APP_VERSION_SHORT);

const char *getDeviceName()
{
    uint8_t dmac[6];

    getMacAddr(dmac);

    // Meshtastic_ab3c or Shortname_abcd
    static char name[20];
    snprintf(name, sizeof(name), "%02x%02x", dmac[4], dmac[5]);
    // if the shortname exists and is NOT the new default of ab3c, use it for BLE name.
    if (strcmp(owner.short_name, name) != 0) {
        snprintf(name, sizeof(name), "%s_%02x%02x", owner.short_name, dmac[4], dmac[5]);
    } else {
        snprintf(name, sizeof(name), "Meshtastic_%02x%02x", dmac[4], dmac[5]);
    }
    return name;
}

#if defined(ELECROW_ThinkNode_M1) || defined(ELECROW_ThinkNode_M2)
static int32_t ledBlinkCount = 0;

static int32_t elecrowLedBlinker()
{
    // are we in alert buzzer mode?
#if HAS_BUTTON
    if (buttonThread->isBuzzing()) {
        // blink LED three times for 3 seconds, then 3 times for a second, with one second pause
        if (ledBlinkCount % 2) { // odd means LED OFF
            ledBlink.set(false);
            ledBlinkCount++;
            if (ledBlinkCount >= 12)
                ledBlinkCount = 0;
            noTone(PIN_BUZZER);
            return 1000;
        } else {
            if (ledBlinkCount < 6) {
                ledBlink.set(true);
                tone(PIN_BUZZER, 4000, 3000);
                ledBlinkCount++;
                return 3000;
            } else {
                ledBlink.set(true);
                tone(PIN_BUZZER, 4000, 1000);
                ledBlinkCount++;
                return 1000;
            }
        }
    } else {
#endif
        ledBlinkCount = 0;
        if (config.device.led_heartbeat_disabled)
            return 1000;

        static bool ledOn;
        // remain on when fully charged or discharging above 10%
        if ((powerStatus->getIsCharging() && powerStatus->getBatteryChargePercent() >= 100) ||
            (!powerStatus->getIsCharging() && powerStatus->getBatteryChargePercent() >= 10)) {
            ledOn = true;
        } else {
            ledOn ^= 1;
        }
        ledBlink.set(ledOn);
        // when charging, blink 0.5Hz square wave rate to indicate that
        if (powerStatus->getIsCharging()) {
            return 500;
        }
        // Blink rapidly when almost empty or if battery is not connected
        if ((!powerStatus->getIsCharging() && powerStatus->getBatteryChargePercent() < 10) || !powerStatus->getHasBattery()) {
            return 250;
        }
#if HAS_BUTTON
    }
#endif
    return 1000;
}
#else
static int32_t ledBlinker()
{
    // Still set up the blinking (heartbeat) interval but skip code path below, so LED will blink if
    // config.device.led_heartbeat_disabled is changed
    if (config.device.led_heartbeat_disabled)
        return 1000;

    static bool ledOn;
    ledOn ^= 1;

    ledBlink.set(ledOn);

    // have a very sparse duty cycle of LED being on, unless charging, then blink 0.5Hz square wave rate to indicate that
    return powerStatus->getIsCharging() ? 1000 : (ledOn ? 1 : 1000);
}
#endif

uint32_t timeLastPowered = 0;

static Periodic *ledPeriodic;
static OSThread *powerFSMthread;
static OSThread *ambientLightingThread;

RadioInterface *rIf = NULL;
#ifdef ARCH_PORTDUINO
RadioLibHal *RadioLibHAL = NULL;
#endif

/**
 * Some platforms (nrf52) might provide an alterate version that suppresses calling delay from sleep.
 */
__attribute__((weak, noinline)) bool loopCanSleep()
{
    return true;
}

// Weak empty variant initialization function.
// May be redefined by variant files.
void lateInitVariant() __attribute__((weak));
void lateInitVariant() {}

/**
 * Print info as a structured log message (for automated log processing)
 */
void printInfo()
{
    LOG_INFO("S:B:%d,%s", HW_VENDOR, optstr(APP_VERSION));
}
#ifndef PIO_UNIT_TESTING
void setup()
{

#if defined(PIN_POWER_EN)
    pinMode(PIN_POWER_EN, OUTPUT);
    digitalWrite(PIN_POWER_EN, HIGH);
#endif

#ifdef LED_POWER
    pinMode(LED_POWER, OUTPUT);
    digitalWrite(LED_POWER, HIGH);
#endif

#ifdef USER_LED
    pinMode(USER_LED, OUTPUT);
    digitalWrite(USER_LED, LOW);
#endif

#if defined(T_DECK)
    // GPIO10 manages all peripheral power supplies
    // Turn on peripheral power immediately after MUC starts.
    // If some boards are turned on late, ESP32 will reset due to low voltage.
    // ESP32-C3(Keyboard) , MAX98357A(Audio Power Amplifier) ,
    // TF Card , Display backlight(AW9364DNR) , AN48841B(Trackball) , ES7210(Decoder)
    pinMode(KB_POWERON, OUTPUT);
    digitalWrite(KB_POWERON, HIGH);
    // T-Deck has all three SPI peripherals (TFT, SD, LoRa) attached to the same SPI bus
    // We need to initialize all CS pins in advance otherwise there will be SPI communication issues
    // e.g. when detecting the SD card
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    delay(100);
#endif

    concurrency::hasBeenSetup = true;
#if ARCH_PORTDUINO
    SPISettings spiSettings(settingsMap[spiSpeed], MSBFIRST, SPI_MODE0);
#else
    SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);
#endif

#if !HAS_TFT
    meshtastic_Config_DisplayConfig_OledType screen_model =
        meshtastic_Config_DisplayConfig_OledType::meshtastic_Config_DisplayConfig_OledType_OLED_AUTO;
    OLEDDISPLAY_GEOMETRY screen_geometry = GEOMETRY_128_64;
#endif

#ifdef USE_SEGGER
    auto mode = false ? SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL : SEGGER_RTT_MODE_NO_BLOCK_TRIM;
#ifdef NRF52840_XXAA
    auto buflen = 4096; // this board has a fair amount of ram
#else
    auto buflen = 256; // this board has a fair amount of ram
#endif
    SEGGER_RTT_ConfigUpBuffer(SEGGER_STDOUT_CH, NULL, NULL, buflen, mode);
#endif

#ifdef DEBUG_PORT
    consoleInit(); // Set serial baud rate and init our mesh console
#endif

#ifdef UNPHONE
    unphone.printStore();
#endif

#if ARCH_PORTDUINO
    struct timeval tv;
    tv.tv_sec = time(NULL);
    tv.tv_usec = 0;
    perhapsSetRTC(RTCQualityNTP, &tv);
#endif

    powerMonInit();
    serialSinceMsec = millis();

    LOG_INFO("\n\n//\\ E S H T /\\ S T / C\n");

    initDeepSleep();

#if defined(LORA_TCXO_GPIO)
    pinMode(LORA_TCXO_GPIO, OUTPUT);
    digitalWrite(LORA_TCXO_GPIO, HIGH);
#endif

#if defined(VEXT_ENABLE)
    pinMode(VEXT_ENABLE, OUTPUT);
    digitalWrite(VEXT_ENABLE, VEXT_ON_VALUE); // turn on the display power
#endif

#if defined(BIAS_T_ENABLE)
    pinMode(BIAS_T_ENABLE, OUTPUT);
    digitalWrite(BIAS_T_ENABLE, BIAS_T_VALUE); // turn on 5V for GPS Antenna
#endif

#if defined(VTFT_CTRL)
    pinMode(VTFT_CTRL, OUTPUT);
    digitalWrite(VTFT_CTRL, LOW);
#endif

#ifdef RESET_OLED
    pinMode(RESET_OLED, OUTPUT);
    digitalWrite(RESET_OLED, 1);
#endif

#ifdef SENSOR_POWER_CTRL_PIN
    pinMode(SENSOR_POWER_CTRL_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_CTRL_PIN, SENSOR_POWER_ON);
#endif

#ifdef SENSOR_GPS_CONFLICT
    bool sensor_detected = false;
#endif
#ifdef PERIPHERAL_WARMUP_MS
    // Some peripherals may require additional time to stabilize after power is connected
    // e.g. I2C on Heltec Vision Master
    LOG_INFO("Wait for peripherals to stabilize");
    delay(PERIPHERAL_WARMUP_MS);
#endif

#ifdef BUTTON_PIN
#ifdef ARCH_ESP32

#if ESP_ARDUINO_VERSION_MAJOR >= 3
#ifdef BUTTON_NEED_PULLUP
    pinMode(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN, INPUT_PULLUP);
#else
    pinMode(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN, INPUT); // default to BUTTON_PIN
#endif
#else
    pinMode(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN, INPUT); // default to BUTTON_PIN
#ifdef BUTTON_NEED_PULLUP
    gpio_pullup_en((gpio_num_t)(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN));
    delay(10);
#endif
#endif
#endif
#endif

    initSPI();

    OSThread::setup();

#if defined(ELECROW_ThinkNode_M1) || defined(ELECROW_ThinkNode_M2)
    // The ThinkNodes have their own blink logic
    ledPeriodic = new Periodic("Blink", elecrowLedBlinker);
#else
    ledPeriodic = new Periodic("Blink", ledBlinker);
#endif

    fsInit();

#if defined(_SEEED_XIAO_NRF52840_SENSE_H_)

    pinMode(CHARGE_LED, INPUT); // sets to detect if charge LED is on or off to see if USB is plugged in

    pinMode(HICHG, OUTPUT);
    digitalWrite(HICHG, LOW); // 100 mA charging current if set to LOW and 50mA (actually about 20mA) if set to HIGH

    pinMode(BAT_READ, OUTPUT);
    digitalWrite(BAT_READ, LOW); // This is pin P0_14 = 14 and by pullling low to GND it provices path to read on pin 32 (P0,31)
                                 // PIN_VBAT the voltage from divider on XIAO board

#endif

#if !MESHTASTIC_EXCLUDE_I2C
#if defined(I2C_SDA1) && defined(ARCH_RP2040)
    Wire1.setSDA(I2C_SDA1);
    Wire1.setSCL(I2C_SCL1);
    Wire1.begin();
#elif defined(I2C_SDA1) && !defined(ARCH_RP2040)
    Wire1.begin(I2C_SDA1, I2C_SCL1);
#elif WIRE_INTERFACES_COUNT == 2
    Wire1.begin();
#endif

#if defined(I2C_SDA) && defined(ARCH_RP2040)
    Wire.setSDA(I2C_SDA);
    Wire.setSCL(I2C_SCL);
    Wire.begin();
#elif defined(I2C_SDA) && !defined(ARCH_RP2040)
    Wire.begin(I2C_SDA, I2C_SCL);
#elif defined(ARCH_PORTDUINO)
    if (settingsStrings[i2cdev] != "") {
        LOG_INFO("Use %s as I2C device", settingsStrings[i2cdev].c_str());
        Wire.begin(settingsStrings[i2cdev].c_str());
    } else {
        LOG_INFO("No I2C device configured, Skip");
    }
#elif HAS_WIRE
    Wire.begin();
#endif
#endif

#ifdef PIN_LCD_RESET
    // FIXME - move this someplace better, LCD is at address 0x3F
    pinMode(PIN_LCD_RESET, OUTPUT);
    digitalWrite(PIN_LCD_RESET, 0);
    delay(1);
    digitalWrite(PIN_LCD_RESET, 1);
    delay(1);
#endif

#ifdef AQ_SET_PIN
    // RAK-12039 set pin for Air quality sensor. Detectable on I2C after ~3 seconds, so we need to rescan later
    pinMode(AQ_SET_PIN, OUTPUT);
    digitalWrite(AQ_SET_PIN, HIGH);
#endif

#if HAS_TFT
    tftSetup();
#endif

    // Currently only the tbeam has a PMU
    // PMU initialization needs to be placed before i2c scanning
    power = new Power();
    power->setStatusHandler(powerStatus);
    powerStatus->observe(&power->newStatus);
    power->setup(); // Must be after status handler is installed, so that handler gets notified of the initial configuration

#if !MESHTASTIC_EXCLUDE_I2C
    // We need to scan here to decide if we have a screen for nodeDB.init() and because power has been applied to
    // accessories
    auto i2cScanner = std::unique_ptr<ScanI2CTwoWire>(new ScanI2CTwoWire());
#if HAS_WIRE
    LOG_INFO("Scan for i2c devices");
#endif

#if defined(I2C_SDA1) && defined(ARCH_RP2040)
    Wire1.setSDA(I2C_SDA1);
    Wire1.setSCL(I2C_SCL1);
    Wire1.begin();
    i2cScanner->scanPort(ScanI2C::I2CPort::WIRE1);
#elif defined(I2C_SDA1) && !defined(ARCH_RP2040)
    Wire1.begin(I2C_SDA1, I2C_SCL1);
    i2cScanner->scanPort(ScanI2C::I2CPort::WIRE1);
#elif defined(NRF52840_XXAA) && (WIRE_INTERFACES_COUNT == 2)
    i2cScanner->scanPort(ScanI2C::I2CPort::WIRE1);
#endif

#if defined(I2C_SDA) && defined(ARCH_RP2040)
    Wire.setSDA(I2C_SDA);
    Wire.setSCL(I2C_SCL);
    Wire.begin();
    i2cScanner->scanPort(ScanI2C::I2CPort::WIRE);
#elif defined(I2C_SDA) && !defined(ARCH_RP2040)
    Wire.begin(I2C_SDA, I2C_SCL);
    i2cScanner->scanPort(ScanI2C::I2CPort::WIRE);
#elif defined(ARCH_PORTDUINO)
    if (settingsStrings[i2cdev] != "") {
        LOG_INFO("Scan for i2c devices");
        i2cScanner->scanPort(ScanI2C::I2CPort::WIRE);
    }
#elif HAS_WIRE
    i2cScanner->scanPort(ScanI2C::I2CPort::WIRE);
#endif

    auto i2cCount = i2cScanner->countDevices();
    if (i2cCount == 0) {
        LOG_INFO("No I2C devices found");
    } else {
        LOG_INFO("%i I2C devices found", i2cCount);
#ifdef SENSOR_GPS_CONFLICT
        sensor_detected = true;
#endif
    }

#ifdef ARCH_ESP32
    // Don't init display if we don't have one or we are waking headless due to a timer event
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
        LOG_DEBUG("suppress screen wake because this is a headless timer wakeup");
        i2cScanner->setSuppressScreen();
    }
#endif

#if !HAS_TFT
    auto screenInfo = i2cScanner->firstScreen();
    screen_found = screenInfo.type != ScanI2C::DeviceType::NONE ? screenInfo.address : ScanI2C::ADDRESS_NONE;

    if (screen_found.port != ScanI2C::I2CPort::NO_I2C) {
        switch (screenInfo.type) {
        case ScanI2C::DeviceType::SCREEN_SH1106:
            screen_model = meshtastic_Config_DisplayConfig_OledType::meshtastic_Config_DisplayConfig_OledType_OLED_SH1106;
            break;
        case ScanI2C::DeviceType::SCREEN_SSD1306:
            screen_model = meshtastic_Config_DisplayConfig_OledType::meshtastic_Config_DisplayConfig_OledType_OLED_SSD1306;
            break;
        case ScanI2C::DeviceType::SCREEN_ST7567:
        case ScanI2C::DeviceType::SCREEN_UNKNOWN:
        default:
            screen_model = meshtastic_Config_DisplayConfig_OledType::meshtastic_Config_DisplayConfig_OledType_OLED_AUTO;
        }
    }
#endif

#define UPDATE_FROM_SCANNER(FIND_FN)

    auto rtc_info = i2cScanner->firstRTC();
    rtc_found = rtc_info.type != ScanI2C::DeviceType::NONE ? rtc_info.address : rtc_found;

    auto kb_info = i2cScanner->firstKeyboard();

    if (kb_info.type != ScanI2C::DeviceType::NONE) {
        cardkb_found = kb_info.address;
        switch (kb_info.type) {
        case ScanI2C::DeviceType::RAK14004:
            kb_model = 0x02;
            break;
        case ScanI2C::DeviceType::CARDKB:
            kb_model = 0x00;
            break;
        case ScanI2C::DeviceType::TDECKKB:
            // assign an arbitrary value to distinguish from other models
            kb_model = 0x10;
            break;
        case ScanI2C::DeviceType::BBQ10KB:
            // assign an arbitrary value to distinguish from other models
            kb_model = 0x11;
            break;
        case ScanI2C::DeviceType::MPR121KB:
            // assign an arbitrary value to distinguish from other models
            kb_model = 0x37;
            break;
        case ScanI2C::DeviceType::TCA8418KB:
            // assign an arbitrary value to distinguish from other models
            kb_model = 0x84;
            break;
        default:
            // use this as default since it's also just zero
            LOG_WARN("kb_info.type is unknown(0x%02x), setting kb_model=0x00", kb_info.type);
            kb_model = 0x00;
        }
    }

    pmu_found = i2cScanner->exists(ScanI2C::DeviceType::PMU_AXP192_AXP2101);

/*
 * There are a bunch of sensors that have no further logic than to be found and stuffed into the
 * nodeTelemetrySensorsMap singleton. This wraps that logic in a temporary scope to declare the temporary field
 * "found".
 */

// Two supported RGB LED currently
#ifdef HAS_RGB_LED
    rgb_found = i2cScanner->firstRGBLED();
#endif

#ifdef HAS_TPS65233
    // TPS65233 is a power management IC for satellite modems, used in the Dreamcatcher
    // We are switching it off here since we don't use an LNB.
    if (i2cScanner->exists(ScanI2C::DeviceType::TPS65233)) {
        Wire.beginTransmission(TPS65233_ADDR);
        Wire.write(0);   // Register 0
        Wire.write(128); // Turn off the LNB power, keep I2C Control enabled
        Wire.endTransmission();
        Wire.beginTransmission(TPS65233_ADDR);
        Wire.write(1); // Register 1
        Wire.write(0); // Turn off Tone Generator 22kHz
        Wire.endTransmission();
    }
#endif

#if !defined(ARCH_STM32WL)
    auto acc_info = i2cScanner->firstAccelerometer();
    accelerometer_found = acc_info.type != ScanI2C::DeviceType::NONE ? acc_info.address : accelerometer_found;
    LOG_DEBUG("acc_info = %i", acc_info.type);
#endif

    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::BME_680, meshtastic_TelemetrySensorType_BME680);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::BME_280, meshtastic_TelemetrySensorType_BME280);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::BMP_280, meshtastic_TelemetrySensorType_BMP280);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::BMP_3XX, meshtastic_TelemetrySensorType_BMP3XX);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::BMP_085, meshtastic_TelemetrySensorType_BMP085);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::INA260, meshtastic_TelemetrySensorType_INA260);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::INA226, meshtastic_TelemetrySensorType_INA226);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::INA219, meshtastic_TelemetrySensorType_INA219);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::INA3221, meshtastic_TelemetrySensorType_INA3221);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::MAX17048, meshtastic_TelemetrySensorType_MAX17048);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::MCP9808, meshtastic_TelemetrySensorType_MCP9808);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::SHT31, meshtastic_TelemetrySensorType_SHT31);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::SHTC3, meshtastic_TelemetrySensorType_SHTC3);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::LPS22HB, meshtastic_TelemetrySensorType_LPS22);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::QMC6310, meshtastic_TelemetrySensorType_QMC6310);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::QMI8658, meshtastic_TelemetrySensorType_QMI8658);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::QMC5883L, meshtastic_TelemetrySensorType_QMC5883L);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::HMC5883L, meshtastic_TelemetrySensorType_QMC5883L);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::PMSA0031, meshtastic_TelemetrySensorType_PMSA003I);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::RCWL9620, meshtastic_TelemetrySensorType_RCWL9620);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::VEML7700, meshtastic_TelemetrySensorType_VEML7700);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::TSL2591, meshtastic_TelemetrySensorType_TSL25911FN);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::OPT3001, meshtastic_TelemetrySensorType_OPT3001);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::MLX90632, meshtastic_TelemetrySensorType_MLX90632);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::MLX90614, meshtastic_TelemetrySensorType_MLX90614);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::SHT4X, meshtastic_TelemetrySensorType_SHT4X);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::AHT10, meshtastic_TelemetrySensorType_AHT10);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::DFROBOT_LARK, meshtastic_TelemetrySensorType_DFROBOT_LARK);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::ICM20948, meshtastic_TelemetrySensorType_ICM20948);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::MAX30102, meshtastic_TelemetrySensorType_MAX30102);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::CGRADSENS, meshtastic_TelemetrySensorType_RADSENS);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::DFROBOT_RAIN, meshtastic_TelemetrySensorType_DFROBOT_RAIN);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::LTR390UV, meshtastic_TelemetrySensorType_LTR390UV);
    scannerToSensorsMap(i2cScanner, ScanI2C::DeviceType::DPS310, meshtastic_TelemetrySensorType_DPS310);

    i2cScanner.reset();
#endif

#ifdef HAS_SDCARD
    setupSDCard();
#endif

    // LED init

#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_STATE_ON); // turn on for now
#endif

    // Hello
    printInfo();
#ifdef BUILD_EPOCH
    LOG_INFO("Build timestamp: %ld", BUILD_EPOCH);
#endif

#ifdef ARCH_ESP32
    esp32Setup();
#endif

#ifdef ARCH_NRF52
    nrf52Setup();
#endif

#ifdef ARCH_RP2040
    rp2040Setup();
#endif

    // We do this as early as possible because this loads preferences from flash
    // but we need to do this after main cpu init (esp32setup), because we need the random seed set
    nodeDB = new NodeDB;

    // If we're taking on the repeater role, use NextHopRouter and turn off 3V3_S rail because peripherals are not needed
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER) {
        router = new NextHopRouter();
#ifdef PIN_3V3_EN
        digitalWrite(PIN_3V3_EN, LOW);
#endif
    } else
        router = new ReliableRouter();

#if HAS_BUTTON || defined(ARCH_PORTDUINO)
    // Buttons. Moved here cause we need NodeDB to be initialized
    buttonThread = new ButtonThread();
#endif

    // only play start melody when role is not tracker or sensor
    if (config.power.is_power_saving == true &&
        IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_TRACKER,
                  meshtastic_Config_DeviceConfig_Role_TAK_TRACKER, meshtastic_Config_DeviceConfig_Role_SENSOR))
        LOG_DEBUG("Tracker/Sensor: Skip start melody");
    else
        playStartMelody();

#if !HAS_TFT
    // fixed screen override?
    if (config.display.oled != meshtastic_Config_DisplayConfig_OledType_OLED_AUTO)
        screen_model = config.display.oled;
#endif

#if defined(USE_SH1107)
    screen_model = meshtastic_Config_DisplayConfig_OledType_OLED_SH1107; // set dimension of 128x128
    screen_geometry = GEOMETRY_128_128;
#endif

#if defined(USE_SH1107_128_64)
    screen_model = meshtastic_Config_DisplayConfig_OledType_OLED_SH1107; // keep dimension of 128x64
#endif

#if !MESHTASTIC_EXCLUDE_I2C
#if !defined(ARCH_STM32WL)
    if (acc_info.type != ScanI2C::DeviceType::NONE) {
        accelerometerThread = new AccelerometerThread(acc_info.type);
    }
#endif

#if defined(HAS_NEOPIXEL) || defined(UNPHONE) || defined(RGBLED_RED)
    ambientLightingThread = new AmbientLightingThread(ScanI2C::DeviceType::NONE);
#elif !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
    if (rgb_found.type != ScanI2C::DeviceType::NONE) {
        ambientLightingThread = new AmbientLightingThread(rgb_found.type);
    }
#endif
#endif

#ifdef T_WATCH_S3
    drv.begin();
    drv.selectLibrary(1);
    // I2C trigger by sending 'go' command
    drv.setMode(DRV2605_MODE_INTTRIG);
#endif

    // Init our SPI controller (must be before screen and lora)
#ifdef ARCH_RP2040
#ifdef HW_SPI1_DEVICE
    SPI1.setSCK(LORA_SCK);
    SPI1.setTX(LORA_MOSI);
    SPI1.setRX(LORA_MISO);
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    SPI1.begin(false);
#else  // HW_SPI1_DEVICE
    SPI.setSCK(LORA_SCK);
    SPI.setTX(LORA_MOSI);
    SPI.setRX(LORA_MISO);
    SPI.begin(false);
#endif // HW_SPI1_DEVICE
#elif ARCH_PORTDUINO
    if (settingsStrings[spidev] != "ch341") {
        SPI.begin();
    }
#elif !defined(ARCH_ESP32) // ARCH_RP2040
    SPI.begin();
#else
        // ESP32
#if defined(HW_SPI1_DEVICE)
    SPI1.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    LOG_DEBUG("SPI1.begin(SCK=%d, MISO=%d, MOSI=%d, NSS=%d)", LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    SPI1.setFrequency(4000000);
#else
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    LOG_DEBUG("SPI.begin(SCK=%d, MISO=%d, MOSI=%d, NSS=%d)", LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    SPI.setFrequency(4000000);
#endif
#endif

    // Initialize the screen first so we can show the logo while we start up everything else.
#if HAS_SCREEN
    screen = new graphics::Screen(screen_found, screen_model, screen_geometry);
#endif
    // setup TZ prior to time actions.
#if !MESHTASTIC_EXCLUDE_TZ
    LOG_DEBUG("Use compiled/slipstreamed %s", slipstreamTZString); // important, removing this clobbers our magic string
    if (*config.device.tzdef && config.device.tzdef[0] != 0) {
        LOG_DEBUG("Saved TZ: %s ", config.device.tzdef);
        setenv("TZ", config.device.tzdef, 1);
    } else {
        if (strncmp((const char *)slipstreamTZString, "tzpl", 4) == 0) {
            setenv("TZ", "GMT0", 1);
        } else {
            setenv("TZ", (const char *)slipstreamTZString, 1);
            strcpy(config.device.tzdef, (const char *)slipstreamTZString);
        }
    }
    tzset();
    LOG_DEBUG("Set Timezone to %s", getenv("TZ"));
#endif

    readFromRTC(); // read the main CPU RTC at first (in case we can't get GPS time)

#if !MESHTASTIC_EXCLUDE_GPS
    // If we're taking on the repeater role, ignore GPS
#ifdef SENSOR_GPS_CONFLICT
    if (sensor_detected == false) {
#endif
        if (HAS_GPS) {
            if (config.device.role != meshtastic_Config_DeviceConfig_Role_REPEATER &&
                config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT) {
                gps = GPS::createGps();
                if (gps) {
                    gpsStatus->observe(&gps->newStatus);
                } else {
                    LOG_DEBUG("Run without GPS");
                }
            }
        }
#ifdef SENSOR_GPS_CONFLICT
    }
#endif

#endif

    nodeStatus->observe(&nodeDB->newStatus);

#ifdef HAS_I2S
    LOG_DEBUG("Start audio thread");
    audioThread = new AudioThread();
#endif

#ifdef HAS_UDP_MULTICAST
    LOG_DEBUG("Start multicast thread");
    udpHandler = new UdpMulticastHandler();
#ifdef ARCH_PORTDUINO
    // FIXME: portduino does not ever call onNetworkConnected so call it here because I don't know what happen if I call
    // onNetworkConnected there
    if (config.network.enabled_protocols & meshtastic_Config_NetworkConfig_ProtocolFlags_UDP_BROADCAST) {
        udpHandler->start();
    }
#endif
#endif
    service = new MeshService();
    service->init();

    // Now that the mesh service is created, create any modules
    setupModules();

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS
    // After modules are setup, so we can observe modules
    setupNicheGraphics();
#endif

#ifdef LED_PIN
    // Turn LED off after boot, if heartbeat by config
    if (config.device.led_heartbeat_disabled)
        digitalWrite(LED_PIN, HIGH ^ LED_STATE_ON);
#endif

// Do this after service.init (because that clears error_code)
#ifdef HAS_PMU
    if (!pmu_found)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_NO_AXP192); // Record a hardware fault for missing hardware
#endif

#if !MESHTASTIC_EXCLUDE_I2C
// Don't call screen setup until after nodedb is setup (because we need
// the current region name)
#if defined(ST7701_CS) || defined(ST7735_CS) || defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) ||       \
    defined(ST7789_CS) || defined(HX8357_CS) || defined(USE_ST7789) || defined(ILI9488_CS)
    screen->setup();
#elif defined(ARCH_PORTDUINO)
    if (screen_found.port != ScanI2C::I2CPort::NO_I2C || settingsMap[displayPanel]) {
        screen->setup();
    }
#else
    if (screen_found.port != ScanI2C::I2CPort::NO_I2C)
        screen->setup();
#endif
#endif

    screen->print("Started...\n");

#ifdef PIN_PWR_DELAY_MS
    // This may be required to give the peripherals time to power up.
    delay(PIN_PWR_DELAY_MS);
#endif

#ifdef ARCH_PORTDUINO
    const struct {
        configNames cfgName;
        std::string strName;
    } loraModules[] = {{use_rf95, "RF95"},     {use_sx1262, "sx1262"}, {use_sx1268, "sx1268"}, {use_sx1280, "sx1280"},
                       {use_lr1110, "lr1110"}, {use_lr1120, "lr1120"}, {use_lr1121, "lr1121"}, {use_llcc68, "LLCC68"}};
    // as one can't use a function pointer to the class constructor:
    auto loraModuleInterface = [](configNames cfgName, LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq,
                                  RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy) {
        switch (cfgName) {
        case use_rf95:
            return (RadioInterface *)new RF95Interface(hal, cs, irq, rst, busy);
        case use_sx1262:
            return (RadioInterface *)new SX1262Interface(hal, cs, irq, rst, busy);
        case use_sx1268:
            return (RadioInterface *)new SX1268Interface(hal, cs, irq, rst, busy);
        case use_sx1280:
            return (RadioInterface *)new SX1280Interface(hal, cs, irq, rst, busy);
        case use_lr1110:
            return (RadioInterface *)new LR1110Interface(hal, cs, irq, rst, busy);
        case use_lr1120:
            return (RadioInterface *)new LR1120Interface(hal, cs, irq, rst, busy);
        case use_lr1121:
            return (RadioInterface *)new LR1121Interface(hal, cs, irq, rst, busy);
        case use_llcc68:
            return (RadioInterface *)new LLCC68Interface(hal, cs, irq, rst, busy);
        default:
            assert(0); // shouldn't happen
            return (RadioInterface *)nullptr;
        }
    };
    for (auto &loraModule : loraModules) {
        if (settingsMap[loraModule.cfgName] && !rIf) {
            LOG_DEBUG("Activate %s radio on SPI port %s", loraModule.strName.c_str(), settingsStrings[spidev].c_str());
            if (settingsStrings[spidev] == "ch341") {
                RadioLibHAL = ch341Hal;
            } else {
                RadioLibHAL = new LockingArduinoHal(SPI, spiSettings);
            }
            rIf = loraModuleInterface(loraModule.cfgName, (LockingArduinoHal *)RadioLibHAL, settingsMap[cs_pin],
                                      settingsMap[irq_pin], settingsMap[reset_pin], settingsMap[busy_pin]);
            if (!rIf->init()) {
                LOG_WARN("No %s radio", loraModule.strName.c_str());
                delete rIf;
                rIf = NULL;
                exit(EXIT_FAILURE);
            } else {
                LOG_INFO("%s init success", loraModule.strName.c_str());
            }
        }
    }
#elif defined(HW_SPI1_DEVICE)
    LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(SPI1, spiSettings);
#else // HW_SPI1_DEVICE
    LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(SPI, spiSettings);
#endif

    // radio init MUST BE AFTER service.init, so we have our radio config settings (from nodedb init)
#if defined(USE_STM32WLx)
    if (!rIf) {
        rIf = new STM32WLE5JCInterface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("No STM32WL radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("STM32WL init success");
            radioType = STM32WLx_RADIO;
        }
    }
#endif

#if defined(ARCH_PORTDUINO)
    if (!rIf) {
        rIf = new SimRadio;
        if (!rIf->init()) {
            LOG_WARN("No simulated radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("Use SIMULATED radio!");
            radioType = SIM_RADIO;
        }
    }
#endif

#if defined(RF95_IRQ) && RADIOLIB_EXCLUDE_SX127X != 1
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        rIf = new RF95Interface(RadioLibHAL, LORA_CS, RF95_IRQ, RF95_RESET, RF95_DIO1);
        if (!rIf->init()) {
            LOG_WARN("No RF95 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("RF95 init success");
            radioType = RF95_RADIO;
        }
    }
#endif

#if defined(USE_SX1262) && !defined(ARCH_PORTDUINO) && !defined(TCXO_OPTIONAL) && RADIOLIB_EXCLUDE_SX126X != 1
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        auto *sxIf = new SX1262Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
#ifdef SX126X_DIO3_TCXO_VOLTAGE
        sxIf->setTCXOVoltage(SX126X_DIO3_TCXO_VOLTAGE);
#endif
        if (!sxIf->init()) {
            LOG_WARN("No SX1262 radio");
            delete sxIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1262 init success");
            rIf = sxIf;
            radioType = SX1262_RADIO;
        }
    }
#endif

#if defined(USE_SX1262) && !defined(ARCH_PORTDUINO) && defined(TCXO_OPTIONAL)
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        // try using the specified TCXO voltage
        auto *sxIf = new SX1262Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        sxIf->setTCXOVoltage(SX126X_DIO3_TCXO_VOLTAGE);
        if (!sxIf->init()) {
            LOG_WARN("No SX1262 radio with TCXO, Vref %fV", SX126X_DIO3_TCXO_VOLTAGE);
            delete sxIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1262 init success, TCXO, Vref %fV", SX126X_DIO3_TCXO_VOLTAGE);
            rIf = sxIf;
            radioType = SX1262_RADIO;
        }
    }

    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        // If specified TCXO voltage fails, attempt to use DIO3 as a reference instead
        rIf = new SX1262Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("No SX1262 radio with XTAL, Vref 0.0V");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1262 init success, XTAL, Vref 0.0V");
            radioType = SX1262_RADIO;
        }
    }
#endif

#if defined(USE_SX1268)
#if defined(SX126X_DIO3_TCXO_VOLTAGE) && defined(TCXO_OPTIONAL)
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        // try using the specified TCXO voltage
        auto *sxIf = new SX1268Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        sxIf->setTCXOVoltage(SX126X_DIO3_TCXO_VOLTAGE);
        if (!sxIf->init()) {
            LOG_WARN("No SX1268 radio with TCXO, Vref %fV", SX126X_DIO3_TCXO_VOLTAGE);
            delete sxIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1268 init success, TCXO, Vref %fV", SX126X_DIO3_TCXO_VOLTAGE);
            rIf = sxIf;
            radioType = SX1268_RADIO;
        }
    }
#endif
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        rIf = new SX1268Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("No SX1268 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1268 init success");
            radioType = SX1268_RADIO;
        }
    }
#endif

#if defined(USE_LLCC68)
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        rIf = new LLCC68Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("No LLCC68 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("LLCC68 init success");
            radioType = LLCC68_RADIO;
        }
    }
#endif

#if defined(USE_LR1110) && RADIOLIB_EXCLUDE_LR11X0 != 1
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        rIf = new LR1110Interface(RadioLibHAL, LR1110_SPI_NSS_PIN, LR1110_IRQ_PIN, LR1110_NRESET_PIN, LR1110_BUSY_PIN);
        if (!rIf->init()) {
            LOG_WARN("No LR1110 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("LR1110 init success");
            radioType = LR1110_RADIO;
        }
    }
#endif

#if defined(USE_LR1120) && RADIOLIB_EXCLUDE_LR11X0 != 1
    if (!rIf) {
        rIf = new LR1120Interface(RadioLibHAL, LR1120_SPI_NSS_PIN, LR1120_IRQ_PIN, LR1120_NRESET_PIN, LR1120_BUSY_PIN);
        if (!rIf->init()) {
            LOG_WARN("No LR1120 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("LR1120 init success");
            radioType = LR1120_RADIO;
        }
    }
#endif

#if defined(USE_LR1121) && RADIOLIB_EXCLUDE_LR11X0 != 1
    if (!rIf) {
        rIf = new LR1121Interface(RadioLibHAL, LR1121_SPI_NSS_PIN, LR1121_IRQ_PIN, LR1121_NRESET_PIN, LR1121_BUSY_PIN);
        if (!rIf->init()) {
            LOG_WARN("No LR1121 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("LR1121 init success");
            radioType = LR1121_RADIO;
        }
    }
#endif

#if defined(USE_SX1280) && RADIOLIB_EXCLUDE_SX128X != 1
    if (!rIf) {
        rIf = new SX1280Interface(RadioLibHAL, SX128X_CS, SX128X_DIO1, SX128X_RESET, SX128X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("No SX1280 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1280 init success");
            radioType = SX1280_RADIO;
        }
    }
#endif

    // check if the radio chip matches the selected region
    if ((config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) && (!rIf->wideLora())) {
        LOG_WARN("LoRa chip does not support 2.4GHz. Revert to unset");
        config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
        nodeDB->saveToDisk(SEGMENT_CONFIG);
        if (!rIf->reconfigure()) {
            LOG_WARN("Reconfigure failed, rebooting");
            screen->startAlert("Rebooting...");
            rebootAtMsec = millis() + 5000;
        }
    }

    lateInitVariant(); // Do board specific init (see extra_variants/README.md for documentation)

#if !MESHTASTIC_EXCLUDE_MQTT
    mqttInit();
#endif

#ifdef RF95_FAN_EN
    // Ability to disable FAN if PIN has been set with RF95_FAN_EN.
    // Make sure LoRa has been started before disabling FAN.
    if (config.lora.pa_fan_disabled)
        digitalWrite(RF95_FAN_EN, LOW ^ 0);
#endif

#ifndef ARCH_PORTDUINO

        // Initialize Wifi
#if HAS_WIFI
    initWifi();
#endif

#if HAS_ETHERNET
    // Initialize Ethernet
    initEthernet();
#endif
#endif

#if defined(ARCH_ESP32) && !MESHTASTIC_EXCLUDE_WEBSERVER
    // Start web server thread.
    webServerThread = new WebServerThread();
#endif

#ifdef ARCH_PORTDUINO
#if __has_include(<ulfius.h>)
    if (settingsMap[webserverport] != -1) {
        piwebServerThread = new PiWebServerThread();
        std::atexit([] { delete piwebServerThread; });
    }
#endif
    initApiServer(TCPPort);
#endif

    // Start airtime logger thread.
    airTime = new AirTime();

    if (!rIf)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_NO_RADIO);
    else {
        router->addInterface(rIf);

        // Log bit rate to debug output
        LOG_DEBUG("LoRA bitrate = %f bytes / sec", (float(meshtastic_Constants_DATA_PAYLOAD_LEN) /
                                                    (float(rIf->getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN)))) *
                                                       1000);
    }

    // This must be _after_ service.init because we need our preferences loaded from flash to have proper timeout values
    PowerFSM_setup(); // we will transition to ON in a couple of seconds, FIXME, only do this for cold boots, not waking from SDS
    powerFSMthread = new PowerFSMThread();

#if !HAS_TFT
    setCPUFast(false); // 80MHz is fine for our slow peripherals
#endif

#ifdef ARDUINO_ARCH_ESP32
    LOG_DEBUG("Free heap  : %7d bytes", ESP.getFreeHeap());
    LOG_DEBUG("Free PSRAM : %7d bytes", ESP.getFreePsram());
#endif
}

#endif
uint32_t rebootAtMsec;   // If not zero we will reboot at this time (used to reboot shortly after the update completes)
uint32_t shutdownAtMsec; // If not zero we will shutdown at this time (used to shutdown from python or mobile client)

// If a thread does something that might need for it to be rescheduled ASAP it can set this flag
// This will suppress the current delay and instead try to run ASAP.
bool runASAP;

extern meshtastic_DeviceMetadata getDeviceMetadata()
{
    meshtastic_DeviceMetadata deviceMetadata;
    strncpy(deviceMetadata.firmware_version, optstr(APP_VERSION), sizeof(deviceMetadata.firmware_version));
    deviceMetadata.device_state_version = DEVICESTATE_CUR_VER;
    deviceMetadata.canShutdown = pmu_found || HAS_CPU_SHUTDOWN;
    deviceMetadata.hasBluetooth = HAS_BLUETOOTH;
    deviceMetadata.hasWifi = HAS_WIFI;
    deviceMetadata.hasEthernet = HAS_ETHERNET;
    deviceMetadata.role = config.device.role;
    deviceMetadata.position_flags = config.position.position_flags;
    deviceMetadata.hw_model = HW_VENDOR;
    deviceMetadata.hasRemoteHardware = moduleConfig.remote_hardware.enabled;
    deviceMetadata.excluded_modules = meshtastic_ExcludedModules_EXCLUDED_NONE;
#if MESHTASTIC_EXCLUDE_REMOTEHARDWARE
    deviceMetadata.excluded_modules |= meshtastic_ExcludedModules_REMOTEHARDWARE_CONFIG;
#endif
#if MESHTASTIC_EXCLUDE_AUDIO
    deviceMetadata.excluded_modules |= meshtastic_ExcludedModules_AUDIO_CONFIG;
#endif
// Option to explicitly include canned messages for edge cases, e.g. niche graphics
#if (!HAS_SCREEN || NO_EXT_GPIO) || MESHTASTIC_EXCLUDE_CANNEDMESSAGES
    deviceMetadata.excluded_modules |= meshtastic_ExcludedModules_CANNEDMSG_CONFIG;
#endif
#if NO_EXT_GPIO
    deviceMetadata.excluded_modules |= meshtastic_ExcludedModules_EXTNOTIF_CONFIG;
#endif
// Only edge case here is if we apply this a device with built in Accelerometer and want to detect interrupts
// We'll have to macro guard against those targets potentially
#if NO_EXT_GPIO || MESHTASTIC_EXCLUDE_DETECTIONSENSOR
    deviceMetadata.excluded_modules |= meshtastic_ExcludedModules_DETECTIONSENSOR_CONFIG;
#endif
// If we don't have any GPIO and we don't have GPS OR we don't want too - no purpose in having serial config
#if NO_EXT_GPIO && NO_GPS || MESHTASTIC_EXCLUDE_SERIAL
    deviceMetadata.excluded_modules |= meshtastic_ExcludedModules_SERIAL_CONFIG;
#endif
#ifndef ARCH_ESP32
    deviceMetadata.excluded_modules |= meshtastic_ExcludedModules_PAXCOUNTER_CONFIG;
#endif
#if !defined(HAS_RGB_LED) && !RAK_4631
    deviceMetadata.excluded_modules |= meshtastic_ExcludedModules_AMBIENTLIGHTING_CONFIG;
#endif

// No bluetooth on these targets (yet):
// Pico W / 2W may get it at some point
// Portduino and ESP32-C6 are excluded because we don't have a working bluetooth stacks integrated yet.
#if defined(ARCH_RP2040) || defined(ARCH_PORTDUINO) || defined(ARCH_STM32WL) || defined(CONFIG_IDF_TARGET_ESP32C6)
    deviceMetadata.excluded_modules |= meshtastic_ExcludedModules_BLUETOOTH_CONFIG;
#endif

#if defined(ARCH_NRF52) && !HAS_ETHERNET // nrf52 doesn't have network unless it's a RAK ethernet gateway currently
    deviceMetadata.excluded_modules |= meshtastic_ExcludedModules_NETWORK_CONFIG; // No network on nRF52
#elif defined(ARCH_RP2040) && !HAS_WIFI && !HAS_ETHERNET
    deviceMetadata.excluded_modules |= meshtastic_ExcludedModules_NETWORK_CONFIG; // No network on RP2040
#endif

#if !(MESHTASTIC_EXCLUDE_PKI)
    deviceMetadata.hasPKC = true;
#endif
    return deviceMetadata;
}

#if !MESHTASTIC_EXCLUDE_I2C
void scannerToSensorsMap(const std::unique_ptr<ScanI2CTwoWire> &i2cScanner, ScanI2C::DeviceType deviceType,
                         meshtastic_TelemetrySensorType sensorType)
{
    auto found = i2cScanner->find(deviceType);
    if (found.type != ScanI2C::DeviceType::NONE) {
        nodeTelemetrySensorsMap[sensorType].first = found.address.address;
        nodeTelemetrySensorsMap[sensorType].second = i2cScanner->fetchI2CBus(found.address);
    }
}
#endif

#ifndef PIO_UNIT_TESTING
void loop()
{
    runASAP = false;

#ifdef ARCH_ESP32
    esp32Loop();
#endif
#ifdef ARCH_NRF52
    nrf52Loop();
#endif
    powerCommandsCheck();

#ifdef DEBUG_STACK
    static uint32_t lastPrint = 0;
    if (!Throttle::isWithinTimespanMs(lastPrint, 10 * 1000L)) {
        lastPrint = millis();
        meshtastic::printThreadInfo("main");
    }
#endif

    service->loop();

    long delayMsec = mainController.runOrDelay();

    // We want to sleep as long as possible here - because it saves power
    if (!runASAP && loopCanSleep()) {
        mainDelay.delay(delayMsec);
    }
}
#endif
