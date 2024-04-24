#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "ReliableRouter.h"
#include "airtime.h"
#include "buzz.h"

#include "error.h"
#include "power.h"
// #include "debug.h"
#include "FSCommon.h"
#include "RTC.h"
#include "SPILock.h"
#include "concurrency/OSThread.h"
#include "concurrency/Periodic.h"
#include "detect/ScanI2C.h"
#include "detect/ScanI2CTwoWire.h"
#include "detect/axpDebug.h"
#include "detect/einkScan.h"
#include "graphics/RAKled.h"
#include "graphics/Screen.h"
#include "main.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include "modules/Modules.h"
#include "shutdown.h"
#include "sleep.h"
#include "target_specific.h"
#include <Wire.h>
#include <memory>
#include <utility>
// #include <driver/rtc_io.h>

#ifdef ARCH_ESP32
#if !MESHTASTIC_EXCLUDE_WEBSERVER
#include "mesh/http/WebServer.h"
#endif
#if !MESHTASTIC_EXCLUDE_BLUETOOTH
#include "nimble/NimbleBluetooth.h"
NimbleBluetooth *nimbleBluetooth;
#endif
#endif

#ifdef ARCH_NRF52
#include "NRF52Bluetooth.h"
NRF52Bluetooth *nrf52Bluetooth;
#endif

#if HAS_WIFI
#include "mesh/api/WiFiServerAPI.h"
#include "mesh/wifi/WiFiAPClient.h"
#endif

#if HAS_ETHERNET
#include "mesh/api/ethServerAPI.h"
#include "mesh/eth/ethClient.h"
#endif

#if !MESHTASTIC_EXCLUDE_MQTT
#include "mqtt/MQTT.h"
#endif

#include "LLCC68Interface.h"
#include "RF95Interface.h"
#include "SX1262Interface.h"
#include "SX1268Interface.h"
#include "SX1280Interface.h"
#include "detect/LoRaRadioType.h"

#ifdef ARCH_STM32WL
#include "STM32WLE5JCInterface.h"
#endif

#if !HAS_RADIO && defined(ARCH_PORTDUINO)
#include "platform/portduino/SimRadio.h"
#endif

#ifdef ARCH_PORTDUINO
#include "linux/LinuxHardwareI2C.h"
#include "mesh/raspihttp/PiWebServer.h"
#include "platform/portduino/PortduinoGlue.h"
#include <fstream>
#include <iostream>
#include <string>
#endif

#if HAS_BUTTON || defined(ARCH_PORTDUINO)
#include "ButtonThread.h"
#endif

#include "PowerFSMThread.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
#include "AccelerometerThread.h"
#include "AmbientLightingThread.h"
#endif

#ifdef HAS_I2S
#include "AudioThread.h"
AudioThread *audioThread;
#endif

using namespace concurrency;

// We always create a screen object, but we only init it if we find the hardware
graphics::Screen *screen;

// Global power status
meshtastic::PowerStatus *powerStatus = new meshtastic::PowerStatus();

// Global GPS status
meshtastic::GPSStatus *gpsStatus = new meshtastic::GPSStatus();

// Global Node status
meshtastic::NodeStatus *nodeStatus = new meshtastic::NodeStatus();

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

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
ATECCX08A atecc;
#endif

#ifdef T_WATCH_S3
Adafruit_DRV2605 drv;
#endif

// Global LoRa radio type
LoRaRadioType radioType = NO_RADIO;

bool isVibrating = false;

bool eink_found = true;

uint32_t serialSinceMsec;

bool pmu_found;

// Array map of sensor types with i2c address and wire as we'll find in the i2c scan
std::pair<uint8_t, TwoWire *> nodeTelemetrySensorsMap[_meshtastic_TelemetrySensorType_MAX + 1] = {};

Router *router = NULL; // Users of router don't care what sort of subclass implements that API

const char *getDeviceName()
{
    uint8_t dmac[6];

    getMacAddr(dmac);

    // Meshtastic_ab3c or Shortname_abcd
    static char name[20];
    snprintf(name, sizeof(name), "%02x%02x", dmac[4], dmac[5]);
    // if the shortname exists and is NOT the new default of ab3c, use it for BLE name.
    if ((owner.short_name != NULL) && (strcmp(owner.short_name, name) != 0)) {
        snprintf(name, sizeof(name), "%s_%02x%02x", owner.short_name, dmac[4], dmac[5]);
    } else {
        snprintf(name, sizeof(name), "Meshtastic_%02x%02x", dmac[4], dmac[5]);
    }
    return name;
}

static int32_t ledBlinker()
{
    // Still set up the blinking (heartbeat) interval but skip code path below, so LED will blink if
    // config.device.led_heartbeat_disabled is changed
    if (config.device.led_heartbeat_disabled)
        return 1000;

    static bool ledOn;
    ledOn ^= 1;

    setLed(ledOn);

    // have a very sparse duty cycle of LED being on, unless charging, then blink 0.5Hz square wave rate to indicate that
    return powerStatus->getIsCharging() ? 1000 : (ledOn ? 1 : 1000);
}

uint32_t timeLastPowered = 0;

static Periodic *ledPeriodic;
static OSThread *powerFSMthread;
static OSThread *accelerometerThread;
static OSThread *ambientLightingThread;
SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

RadioInterface *rIf = NULL;

/**
 * Some platforms (nrf52) might provide an alterate version that suppresses calling delay from sleep.
 */
__attribute__((weak, noinline)) bool loopCanSleep()
{
    return true;
}

void setup()
{
    concurrency::hasBeenSetup = true;
    meshtastic_Config_DisplayConfig_OledType screen_model =
        meshtastic_Config_DisplayConfig_OledType::meshtastic_Config_DisplayConfig_OledType_OLED_AUTO;
    OLEDDISPLAY_GEOMETRY screen_geometry = GEOMETRY_128_64;

#ifdef SEGGER_STDOUT_CH
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

    serialSinceMsec = millis();

    LOG_INFO("\n\n//\\ E S H T /\\ S T / C\n\n");

    initDeepSleep();

    // power on peripherals
#if defined(TTGO_T_ECHO) && defined(PIN_POWER_EN)
    pinMode(PIN_POWER_EN, OUTPUT);
    digitalWrite(PIN_POWER_EN, HIGH);
    // digitalWrite(PIN_POWER_EN1, INPUT);
#endif

#if defined(LORA_TCXO_GPIO)
    pinMode(LORA_TCXO_GPIO, OUTPUT);
    digitalWrite(LORA_TCXO_GPIO, HIGH);
#endif

#if defined(VEXT_ENABLE_V03)
    pinMode(VEXT_ENABLE_V03, OUTPUT);
    pinMode(ST7735_BL_V03, OUTPUT);
    digitalWrite(VEXT_ENABLE_V03, 0); // turn on the display power and antenna boost
    digitalWrite(ST7735_BL_V03, 1);   // display backligth on
    LOG_DEBUG("HELTEC Detect Tracker V1.0\n");
#elif defined(VEXT_ENABLE_V05)
    pinMode(VEXT_ENABLE_V05, OUTPUT);
    pinMode(ST7735_BL_V05, OUTPUT);
    digitalWrite(VEXT_ENABLE_V05, 1); // turn on the lora antenna boost
    digitalWrite(ST7735_BL_V05, 1);   // turn on display backligth
    LOG_DEBUG("HELTEC Detect Tracker V1.1\n");
#elif defined(VEXT_ENABLE)
    pinMode(VEXT_ENABLE, OUTPUT);
    digitalWrite(VEXT_ENABLE, 0); // turn on the display power
#endif

#if defined(VGNSS_CTRL_V03)
    pinMode(VGNSS_CTRL_V03, OUTPUT);
    digitalWrite(VGNSS_CTRL_V03, LOW);
#endif

#if defined(VTFT_CTRL_V03)
    pinMode(VTFT_CTRL_V03, OUTPUT);
    digitalWrite(VTFT_CTRL_V03, LOW);
#endif

#if defined(VGNSS_CTRL)
    pinMode(VGNSS_CTRL, OUTPUT);
    digitalWrite(VGNSS_CTRL, LOW);
#endif

#if defined(VTFT_CTRL)
    pinMode(VTFT_CTRL, OUTPUT);
    digitalWrite(VTFT_CTRL, LOW);
#endif

#ifdef RESET_OLED
    pinMode(RESET_OLED, OUTPUT);
    digitalWrite(RESET_OLED, 1);
#endif

#ifdef BUTTON_PIN
#ifdef ARCH_ESP32

    // If the button is connected to GPIO 12, don't enable the ability to use
    // meshtasticAdmin on the device.
    pinMode(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN, INPUT);

#ifdef BUTTON_NEED_PULLUP
    gpio_pullup_en((gpio_num_t)(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN));
    delay(10);
#endif

#endif
#endif

    OSThread::setup();

    ledPeriodic = new Periodic("Blink", ledBlinker);

    fsInit();

#if defined(_SEEED_XIAO_NRF52840_SENSE_H_)

    pinMode(CHARGE_LED, INPUT); // sets to detect if charge LED is on or off to see if USB is plugged in

    pinMode(HICHG, OUTPUT);
    digitalWrite(HICHG, LOW); // 100 mA charging current if set to LOW and 50mA (actually about 20mA) if set to HIGH

    pinMode(BAT_READ, OUTPUT);
    digitalWrite(BAT_READ, LOW); // This is pin P0_14 = 14 and by pullling low to GND it provices path to read on pin 32 (P0,31)
                                 // PIN_VBAT the voltage from divider on XIAO board

#endif

#if defined(I2C_SDA1) && defined(ARCH_RP2040)
    Wire1.setSDA(I2C_SDA1);
    Wire1.setSCL(I2C_SCL1);
    Wire1.begin();
#elif defined(I2C_SDA1) && !defined(ARCH_RP2040)
    Wire1.begin(I2C_SDA1, I2C_SCL1);
#endif

#if defined(I2C_SDA) && defined(ARCH_RP2040)
    Wire.setSDA(I2C_SDA);
    Wire.setSCL(I2C_SCL);
    Wire.begin();
#elif defined(I2C_SDA) && !defined(ARCH_RP2040)
    Wire.begin(I2C_SDA, I2C_SCL);
#elif defined(ARCH_PORTDUINO)
    if (settingsStrings[i2cdev] != "") {
        LOG_INFO("Using %s as I2C device.\n", settingsStrings[i2cdev]);
        Wire.begin(settingsStrings[i2cdev].c_str());
    } else {
        LOG_INFO("No I2C device configured, skipping.\n");
    }
#elif HAS_WIRE
    Wire.begin();
#endif

#ifdef PIN_LCD_RESET
    // FIXME - move this someplace better, LCD is at address 0x3F
    pinMode(PIN_LCD_RESET, OUTPUT);
    digitalWrite(PIN_LCD_RESET, 0);
    delay(1);
    digitalWrite(PIN_LCD_RESET, 1);
    delay(1);
#endif

#ifdef RAK4630
#ifdef PIN_3V3_EN
    // We need to enable 3.3V periphery in order to scan it
    pinMode(PIN_3V3_EN, OUTPUT);
    digitalWrite(PIN_3V3_EN, HIGH);
#endif
#ifdef AQ_SET_PIN
    // RAK-12039 set pin for Air quality sensor
    pinMode(AQ_SET_PIN, OUTPUT);
    digitalWrite(AQ_SET_PIN, HIGH);
#endif
#endif

#ifdef T_DECK
    // enable keyboard
    pinMode(KB_POWERON, OUTPUT);
    digitalWrite(KB_POWERON, HIGH);
    // There needs to be a delay after power on, give LILYGO-KEYBOARD some startup time
    // otherwise keyboard and touch screen will not work
    delay(800);
#endif

    // Currently only the tbeam has a PMU
    // PMU initialization needs to be placed before i2c scanning
    power = new Power();
    power->setStatusHandler(powerStatus);
    powerStatus->observe(&power->newStatus);
    power->setup(); // Must be after status handler is installed, so that handler gets notified of the initial configuration

    // We need to scan here to decide if we have a screen for nodeDB.init() and because power has been applied to
    // accessories
    auto i2cScanner = std::unique_ptr<ScanI2CTwoWire>(new ScanI2CTwoWire());
#if HAS_WIRE
    LOG_INFO("Scanning for i2c devices...\n");
#endif

#if defined(I2C_SDA1) && defined(ARCH_RP2040)
    Wire1.setSDA(I2C_SDA1);
    Wire1.setSCL(I2C_SCL1);
    Wire1.begin();
    i2cScanner->scanPort(ScanI2C::I2CPort::WIRE1);
#elif defined(I2C_SDA1) && !defined(ARCH_RP2040)
    Wire1.begin(I2C_SDA1, I2C_SCL1);
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
        LOG_INFO("Scanning for i2c devices...\n");
        i2cScanner->scanPort(ScanI2C::I2CPort::WIRE);
    }
#elif HAS_WIRE
    i2cScanner->scanPort(ScanI2C::I2CPort::WIRE);
#endif

    auto i2cCount = i2cScanner->countDevices();
    if (i2cCount == 0) {
        LOG_INFO("No I2C devices found\n");
        Wire.end();
#ifdef I2C_SDA1
        Wire1.end();
#endif
    } else {
        LOG_INFO("%i I2C devices found\n", i2cCount);
    }

#ifdef ARCH_ESP32
    // Don't init display if we don't have one or we are waking headless due to a timer event
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
        LOG_DEBUG("suppress screen wake because this is a headless timer wakeup");
        i2cScanner->setSuppressScreen();
    }
#endif

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
        default:
            // use this as default since it's also just zero
            LOG_WARN("kb_info.type is unknown(0x%02x), setting kb_model=0x00\n", kb_info.type);
            kb_model = 0x00;
        }
    }

    pmu_found = i2cScanner->exists(ScanI2C::DeviceType::PMU_AXP192_AXP2101);

/*
 * There are a bunch of sensors that have no further logic than to be found and stuffed into the
 * nodeTelemetrySensorsMap singleton. This wraps that logic in a temporary scope to declare the temporary field
 * "found".
 */

// Only one supported RGB LED currently
#ifdef HAS_NCP5623
    rgb_found = i2cScanner->find(ScanI2C::DeviceType::NCP5623);
#endif

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
    auto acc_info = i2cScanner->firstAccelerometer();
    accelerometer_found = acc_info.type != ScanI2C::DeviceType::NONE ? acc_info.address : accelerometer_found;
    LOG_DEBUG("acc_info = %i\n", acc_info.type);
#endif

#define STRING(S) #S

#define SCANNER_TO_SENSORS_MAP(SCANNER_T, PB_T)                                                                                  \
    {                                                                                                                            \
        auto found = i2cScanner->find(SCANNER_T);                                                                                \
        if (found.type != ScanI2C::DeviceType::NONE) {                                                                           \
            nodeTelemetrySensorsMap[PB_T].first = found.address.address;                                                         \
            nodeTelemetrySensorsMap[PB_T].second = i2cScanner->fetchI2CBus(found.address);                                       \
            LOG_DEBUG("found i2c sensor %s\n", STRING(PB_T));                                                                    \
        }                                                                                                                        \
    }

    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::BME_680, meshtastic_TelemetrySensorType_BME680)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::BME_280, meshtastic_TelemetrySensorType_BME280)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::BMP_280, meshtastic_TelemetrySensorType_BMP280)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::BMP_085, meshtastic_TelemetrySensorType_BMP085)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::INA260, meshtastic_TelemetrySensorType_INA260)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::INA219, meshtastic_TelemetrySensorType_INA219)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::INA3221, meshtastic_TelemetrySensorType_INA3221)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::MCP9808, meshtastic_TelemetrySensorType_MCP9808)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::MCP9808, meshtastic_TelemetrySensorType_MCP9808)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::SHT31, meshtastic_TelemetrySensorType_SHT31)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::SHTC3, meshtastic_TelemetrySensorType_SHTC3)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::LPS22HB, meshtastic_TelemetrySensorType_LPS22)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::QMC6310, meshtastic_TelemetrySensorType_QMC6310)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::QMI8658, meshtastic_TelemetrySensorType_QMI8658)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::QMC5883L, meshtastic_TelemetrySensorType_QMC5883L)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::PMSA0031, meshtastic_TelemetrySensorType_PMSA003I)

    i2cScanner.reset();

#ifdef HAS_SDCARD
    setupSDCard();
#endif

#ifdef RAK4630
    // scanEInkDevice();
#endif

    // LED init

#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, 1 ^ LED_INVERTED); // turn on for now
#endif

    // Hello
    LOG_INFO("Meshtastic hwvendor=%d, swver=%s\n", HW_VENDOR, optstr(APP_VERSION));

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

    // If we're taking on the repeater role, use flood router and turn off 3V3_S rail because peripherals are not needed
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER) {
        router = new FloodingRouter();
#ifdef PIN_3V3_EN
        digitalWrite(PIN_3V3_EN, LOW);
#endif
    } else
        router = new ReliableRouter();

#if HAS_BUTTON || defined(ARCH_PORTDUINO)
    // Buttons. Moved here cause we need NodeDB to be initialized
    buttonThread = new ButtonThread();
#endif

    playStartMelody();

    // fixed screen override?
    if (config.display.oled != meshtastic_Config_DisplayConfig_OledType_OLED_AUTO)
        screen_model = config.display.oled;

#if defined(USE_SH1107)
    screen_model = meshtastic_Config_DisplayConfig_OledType_OLED_SH1107; // set dimension of 128x128
    display_geometry = GEOMETRY_128_128;
#endif

#if defined(USE_SH1107_128_64)
    screen_model = meshtastic_Config_DisplayConfig_OledType_OLED_SH1107; // keep dimension of 128x64
#endif

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
    if (acc_info.type != ScanI2C::DeviceType::NONE) {
        config.display.wake_on_tap_or_motion = true;
        moduleConfig.external_notification.enabled = true;
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

#ifdef T_WATCH_S3
    drv.begin();
    drv.selectLibrary(1);
    // I2C trigger by sending 'go' command
    drv.setMode(DRV2605_MODE_INTTRIG);
#endif

    // Init our SPI controller (must be before screen and lora)
    initSPI();
#ifdef ARCH_RP2040
#ifdef HW_SPI1_DEVICE
    SPI1.setSCK(LORA_SCK);
    SPI1.setTX(LORA_MOSI);
    SPI1.setRX(LORA_MISO);
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    SPI1.begin(false);
#else                      // HW_SPI1_DEVICE
    SPI.setSCK(LORA_SCK);
    SPI.setTX(LORA_MOSI);
    SPI.setRX(LORA_MISO);
    SPI.begin(false);
#endif                     // HW_SPI1_DEVICE
#elif !defined(ARCH_ESP32) // ARCH_RP2040
    SPI.begin();
#else
    // ESP32
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    LOG_DEBUG("SPI.begin(SCK=%d, MISO=%d, MOSI=%d, NSS=%d)\n", LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    SPI.setFrequency(4000000);
#endif

    // Initialize the screen first so we can show the logo while we start up everything else.
    screen = new graphics::Screen(screen_found, screen_model, screen_geometry);

    // setup TZ prior to time actions.
    if (*config.device.tzdef) {
        setenv("TZ", config.device.tzdef, 1);
    } else {
        setenv("TZ", "GMT0", 1);
    }
    tzset();
    LOG_DEBUG("Set Timezone to %s\n", getenv("TZ"));

    readFromRTC(); // read the main CPU RTC at first (in case we can't get GPS time)

#if !MESHTASTIC_EXCLUDE_GPS
    // If we're taking on the repeater role, ignore GPS
    if (HAS_GPS) {
        if (config.device.role != meshtastic_Config_DeviceConfig_Role_REPEATER &&
            config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT) {
            gps = GPS::createGps();
            if (gps) {
                gpsStatus->observe(&gps->newStatus);
            } else {
                LOG_DEBUG("Running without GPS.\n");
            }
        }
    }
#endif

    nodeStatus->observe(&nodeDB->newStatus);

#ifdef HAS_I2S
    LOG_DEBUG("Starting audio thread\n");
    audioThread = new AudioThread();
#endif

    service.init();

    // Now that the mesh service is created, create any modules
    setupModules();

// Do this after service.init (because that clears error_code)
#ifdef HAS_PMU
    if (!pmu_found)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_NO_AXP192); // Record a hardware fault for missing hardware
#endif

// Don't call screen setup until after nodedb is setup (because we need
// the current region name)
#if defined(ST7735_CS) || defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ST7789_CS) || defined(HX8357_CS)
    screen->setup();
#elif defined(ARCH_PORTDUINO)
    if (screen_found.port != ScanI2C::I2CPort::NO_I2C || settingsMap[displayPanel]) {
        screen->setup();
    }
#else
    if (screen_found.port != ScanI2C::I2CPort::NO_I2C)
        screen->setup();
#endif

    screen->print("Started...\n");

#ifdef SX126X_ANT_SW
    // make analog PA vs not PA switch on SX126x eval board work properly
    pinMode(SX126X_ANT_SW, OUTPUT);
    digitalWrite(SX126X_ANT_SW, 1);
#endif

#ifdef PIN_PWR_DELAY_MS
    // This may be required to give the peripherals time to power up.
    delay(PIN_PWR_DELAY_MS);
#endif

#ifdef ARCH_PORTDUINO
    if (settingsMap[use_sx1262]) {
        if (!rIf) {
            LOG_DEBUG("Attempting to activate sx1262 radio on SPI port %s\n", settingsStrings[spidev].c_str());
            LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(*LoraSPI, spiSettings);
            rIf = new SX1262Interface((LockingArduinoHal *)RadioLibHAL, settingsMap[cs], settingsMap[irq], settingsMap[reset],
                                      settingsMap[busy]);
            if (!rIf->init()) {
                LOG_ERROR("Failed to find SX1262 radio\n");
                delete rIf;
                exit(EXIT_FAILURE);
            } else {
                LOG_INFO("SX1262 Radio init succeeded, using SX1262 radio\n");
            }
        }
    } else if (settingsMap[use_rf95]) {
        if (!rIf) {
            LOG_DEBUG("Attempting to activate rf95 radio on SPI port %s\n", settingsStrings[spidev].c_str());
            LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(*LoraSPI, spiSettings);
            rIf = new RF95Interface((LockingArduinoHal *)RadioLibHAL, settingsMap[cs], settingsMap[irq], settingsMap[reset],
                                    settingsMap[busy]);
            if (!rIf->init()) {
                LOG_ERROR("Failed to find RF95 radio\n");
                delete rIf;
                rIf = NULL;
                exit(EXIT_FAILURE);
            } else {
                LOG_INFO("RF95 Radio init succeeded, using RF95 radio\n");
            }
        }
    } else if (settingsMap[use_sx1280]) {
        if (!rIf) {
            LOG_DEBUG("Attempting to activate sx1280 radio on SPI port %s\n", settingsStrings[spidev].c_str());
            LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(*LoraSPI, spiSettings);
            rIf = new SX1280Interface((LockingArduinoHal *)RadioLibHAL, settingsMap[cs], settingsMap[irq], settingsMap[reset],
                                      settingsMap[busy]);
            if (!rIf->init()) {
                LOG_ERROR("Failed to find SX1280 radio\n");
                delete rIf;
                rIf = NULL;
                exit(EXIT_FAILURE);
            } else {
                LOG_INFO("SX1280 Radio init succeeded, using SX1280 radio\n");
            }
        }
    } else if (settingsMap[use_sx1268]) {
        if (!rIf) {
            LOG_DEBUG("Attempting to activate sx1268 radio on SPI port %s\n", settingsStrings[spidev].c_str());
            LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(*LoraSPI, spiSettings);
            rIf = new SX1268Interface((LockingArduinoHal *)RadioLibHAL, settingsMap[cs], settingsMap[irq], settingsMap[reset],
                                      settingsMap[busy]);
            if (!rIf->init()) {
                LOG_ERROR("Failed to find SX1268 radio\n");
                delete rIf;
                rIf = NULL;
                exit(EXIT_FAILURE);
            } else {
                LOG_INFO("SX1268 Radio init succeeded, using SX1268 radio\n");
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
            LOG_WARN("Failed to find STM32WL radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("STM32WL Radio init succeeded, using STM32WL radio\n");
            radioType = STM32WLx_RADIO;
        }
    }
#endif

#if !HAS_RADIO && defined(ARCH_PORTDUINO)
    if (!rIf) {
        rIf = new SimRadio;
        if (!rIf->init()) {
            LOG_WARN("Failed to find simulated radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("Using SIMULATED radio!\n");
            radioType = SIM_RADIO;
        }
    }
#endif

#if defined(RF95_IRQ)
    if (!rIf) {
        rIf = new RF95Interface(RadioLibHAL, LORA_CS, RF95_IRQ, RF95_RESET, RF95_DIO1);
        if (!rIf->init()) {
            LOG_WARN("Failed to find RF95 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("RF95 Radio init succeeded, using RF95 radio\n");
            radioType = RF95_RADIO;
        }
    }
#endif

#if defined(USE_SX1262) && !defined(ARCH_PORTDUINO)
    if (!rIf) {
        rIf = new SX1262Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("Failed to find SX1262 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1262 Radio init succeeded, using SX1262 radio\n");
            radioType = SX1262_RADIO;
        }
    }
#endif

#if defined(USE_SX1268)
    if (!rIf) {
        rIf = new SX1268Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("Failed to find SX1268 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1268 Radio init succeeded, using SX1268 radio\n");
            radioType = SX1268_RADIO;
        }
    }
#endif

#if defined(USE_LLCC68)
    if (!rIf) {
        rIf = new LLCC68Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("Failed to find LLCC68 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("LLCC68 Radio init succeeded, using LLCC68 radio\n");
            radioType = LLCC68_RADIO;
        }
    }
#endif

#if defined(USE_SX1280)
    if (!rIf) {
        rIf = new SX1280Interface(RadioLibHAL, SX128X_CS, SX128X_DIO1, SX128X_RESET, SX128X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("Failed to find SX1280 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1280 Radio init succeeded, using SX1280 radio\n");
            radioType = SX1280_RADIO;
        }
    }
#endif

    // check if the radio chip matches the selected region

    if ((config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) && (!rIf->wideLora())) {
        LOG_WARN("Radio chip does not support 2.4GHz LoRa. Reverting to unset.\n");
        config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
        nodeDB->saveToDisk(SEGMENT_CONFIG);
        if (!rIf->reconfigure()) {
            LOG_WARN("Reconfigure failed, rebooting\n");
            screen->startRebootScreen();
            rebootAtMsec = millis() + 5000;
        }
    }

#if !MESHTASTIC_EXCLUDE_MQTT
    mqttInit();
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
        LOG_DEBUG("LoRA bitrate = %f bytes / sec\n", (float(meshtastic_Constants_DATA_PAYLOAD_LEN) /
                                                      (float(rIf->getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN)))) *
                                                         1000);
    }

    // This must be _after_ service.init because we need our preferences loaded from flash to have proper timeout values
    PowerFSM_setup(); // we will transition to ON in a couple of seconds, FIXME, only do this for cold boots, not waking from SDS
    powerFSMthread = new PowerFSMThread();
    setCPUFast(false); // 80MHz is fine for our slow peripherals
}

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
    return deviceMetadata;
}

void loop()
{
    runASAP = false;

    // axpDebugOutput.loop();

    // heap_caps_check_integrity_all(true); // FIXME - disable this expensive check

#ifdef ARCH_ESP32
    esp32Loop();
#endif
#ifdef ARCH_NRF52
    nrf52Loop();
#endif
    powerCommandsCheck();

    // For debugging
    // if (rIf) ((RadioLibInterface *)rIf)->isActivelyReceiving();

#ifdef DEBUG_STACK
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 10 * 1000L) {
        lastPrint = millis();
        meshtastic::printThreadInfo("main");
    }
#endif

    // TODO: This should go into a thread handled by FreeRTOS.
    // handleWebResponse();

    service.loop();

    long delayMsec = mainController.runOrDelay();

    /* if (mainController.nextThread && delayMsec)
        LOG_DEBUG("Next %s in %ld\n", mainController.nextThread->ThreadName.c_str(),
                  mainController.nextThread->tillRun(millis())); */

    // We want to sleep as long as possible here - because it saves power
    if (!runASAP && loopCanSleep()) {
        // if(delayMsec > 100) LOG_DEBUG("sleeping %ld\n", delayMsec);
        mainDelay.delay(delayMsec);
    }
    // if (didWake) LOG_DEBUG("wake!\n");
}
