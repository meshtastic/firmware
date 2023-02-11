#include "GPS.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "ReliableRouter.h"
#include "airtime.h"
#include "buzz.h"
#include "configuration.h"
#include "error.h"
#include "power.h"
// #include "debug.h"
#include "FSCommon.h"
#include "RTC.h"
#include "SPILock.h"
#include "concurrency/OSThread.h"
#include "concurrency/Periodic.h"
#include "detect/axpDebug.h"
#include "detect/einkScan.h"
#include "detect/i2cScan.h"
#include "graphics/Screen.h"
#include "main.h"
#include "modules/Modules.h"
#include "shutdown.h"
#include "sleep.h"
#include "target_specific.h"
#include <Wire.h>
// #include <driver/rtc_io.h>

#include "mesh/eth/ethClient.h"
#include "mesh/http/WiFiAPClient.h"

#ifdef ARCH_ESP32
#include "mesh/http/WebServer.h"
#include "nimble/NimbleBluetooth.h"
NimbleBluetooth *nimbleBluetooth;
#endif

#ifdef ARCH_NRF52
#include "NRF52Bluetooth.h"
NRF52Bluetooth *nrf52Bluetooth;
#endif

#if HAS_WIFI
#include "mesh/api/WiFiServerAPI.h"
#include "mqtt/MQTT.h"
#endif

#if HAS_ETHERNET
#include "mesh/api/ethServerAPI.h"
#include "mqtt/MQTT.h"
#endif

#include "LLCC68Interface.h"
#include "RF95Interface.h"
#include "SX1262Interface.h"
#include "SX1268Interface.h"
#include "SX1280Interface.h"
#if !HAS_RADIO && defined(ARCH_PORTDUINO)
#include "platform/portduino/SimRadio.h"
#endif

#if HAS_BUTTON
#include "ButtonThread.h"
#endif
#include "PowerFSMThread.h"

using namespace concurrency;

// We always create a screen object, but we only init it if we find the hardware
graphics::Screen *screen;

// Global power status
meshtastic::PowerStatus *powerStatus = new meshtastic::PowerStatus();

// Global GPS status
meshtastic::GPSStatus *gpsStatus = new meshtastic::GPSStatus();

// Global Node status
meshtastic::NodeStatus *nodeStatus = new meshtastic::NodeStatus();

/// The I2C address of our display (if found)
uint8_t screen_found;
uint8_t screen_model;

// The I2C address of the cardkb or RAK14004 (if found)
uint8_t cardkb_found;
// 0x02 for RAK14004 and 0x00 for cardkb
uint8_t kb_model;

// The I2C address of the RTC Module (if found)
uint8_t rtc_found;

// Keystore Chips
uint8_t keystore_found;
#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
ATECCX08A atecc;
#endif

bool eink_found = true;

uint32_t serialSinceMsec;

bool pmu_found;

// Array map of sensor types (as array index) and i2c address as value we'll find in the i2c scan
uint8_t nodeTelemetrySensorsMap[_meshtastic_TelemetrySensorType_MAX + 1] = {
    0}; // one is enough, missing elements will be initialized to 0 anyway.

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
    static bool ledOn;
    ledOn ^= 1;

    setLed(ledOn);

#ifdef ARCH_ESP32
    auto newHeap = ESP.getFreeHeap();
    if (newHeap < 11000) {
        LOG_DEBUG("\n\n====== heap too low [11000] -> reboot in 1s ======\n\n");
#ifdef HAS_SCREEN
        screen->startRebootScreen();
#endif
        rebootAtMsec = millis() + 900;
    }
#endif

    // have a very sparse duty cycle of LED being on, unless charging, then blink 0.5Hz square wave rate to indicate that
    return powerStatus->getIsCharging() ? 1000 : (ledOn ? 1 : 1000);
}

uint32_t timeLastPowered = 0;

#if HAS_BUTTON
bool ButtonThread::shutdown_on_long_stop = false;
#endif

static Periodic *ledPeriodic;
static OSThread *powerFSMthread;
#if HAS_BUTTON
static OSThread *buttonThread;
uint32_t ButtonThread::longPressTime = 0;
#endif

RadioInterface *rIf = NULL;

/**
 * Some platforms (nrf52) might provide an alterate version that supresses calling delay from sleep.
 */
__attribute__((weak, noinline)) bool loopCanSleep()
{
    return true;
}

void setup()
{
    concurrency::hasBeenSetup = true;

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

    // Testing this fix für erratic T-Echo boot behaviour
#if defined(TTGO_T_ECHO) && defined(PIN_EINK_PWR_ON)
    pinMode(PIN_EINK_PWR_ON, OUTPUT);
    digitalWrite(PIN_EINK_PWR_ON, HIGH);
#endif

#ifdef VEXT_ENABLE
    pinMode(VEXT_ENABLE, OUTPUT);
    digitalWrite(VEXT_ENABLE, 0); // turn on the display power
#endif

#ifdef RESET_OLED
    pinMode(RESET_OLED, OUTPUT);
    digitalWrite(RESET_OLED, 1);
#endif

#ifdef BUTTON_PIN
#ifdef ARCH_ESP32

    // If the button is connected to GPIO 12, don't enable the ability to use
    // meshtasticAdmin on the device.
    pinMode(BUTTON_PIN, INPUT);

#ifdef BUTTON_NEED_PULLUP
    gpio_pullup_en((gpio_num_t)BUTTON_PIN);
    delay(10);
#endif

#endif
#endif

    OSThread::setup();

    ledPeriodic = new Periodic("Blink", ledBlinker);

    fsInit();

    router = new ReliableRouter();

#ifdef I2C_SDA1
    Wire1.begin(I2C_SDA1, I2C_SCL1);
#endif

#ifdef I2C_SDA
    Wire.begin(I2C_SDA, I2C_SCL);
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
    // We need to enable 3.3V periphery in order to scan it
    pinMode(PIN_3V3_EN, OUTPUT);
    digitalWrite(PIN_3V3_EN, HIGH);
#endif

    // Currently only the tbeam has a PMU
    // PMU initialization needs to be placed before scanI2Cdevice
    power = new Power();
    power->setStatusHandler(powerStatus);
    powerStatus->observe(&power->newStatus);
    power->setup(); // Must be after status handler is installed, so that handler gets notified of the initial configuration

#ifdef LILYGO_TBEAM_S3_CORE
    // In T-Beam-S3-core, the I2C device cannot be scanned before power initialization, otherwise the device will be stuck
    // PCF8563 RTC in tbeam-s3 uses Wire1 to share I2C bus
    Wire1.beginTransmission(PCF8563_RTC);
    if (Wire1.endTransmission() == 0) {
        rtc_found = PCF8563_RTC;
        LOG_INFO("PCF8563 RTC found\n");
    }
#endif
    // We need to scan here to decide if we have a screen for nodeDB.init()
    scanI2Cdevice();

#ifdef HAS_SDCARD
    setupSDCard();
#endif

#ifdef RAK4630
    // scanEInkDevice();
#endif

#if HAS_BUTTON
    // Buttons & LED
    buttonThread = new ButtonThread();
#endif

#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, 1 ^ LED_INVERTED); // turn on for now
#endif

    // Hello
    LOG_INFO("Meshtastic hwvendor=%d, swver=%s\n", HW_VENDOR, optstr(APP_VERSION));

#ifdef ARCH_ESP32
    // Don't init display if we don't have one or we are waking headless due to a timer event
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER)
        screen_found = 0; // forget we even have the hardware

    esp32Setup();
#endif

#ifdef ARCH_NRF52
    nrf52Setup();
#endif
    // We do this as early as possible because this loads preferences from flash
    // but we need to do this after main cpu iniot (esp32setup), because we need the random seed set
    nodeDB.init();

    // If we're taking on the repeater role, use flood router
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER)
        router = new FloodingRouter();

    playStartMelody();

    // fixed screen override?
    if (config.display.oled != meshtastic_Config_DisplayConfig_OledType_OLED_AUTO)
        screen_model = config.display.oled;

#if defined(USE_SH1107)
    screen_model = Config_DisplayConfig_OledType_OLED_SH1107; // set dimension of 128x128
#endif

    // Init our SPI controller (must be before screen and lora)
    initSPI();
#ifndef ARCH_ESP32
    SPI.begin();
#else
    // ESP32
    SPI.begin(RF95_SCK, RF95_MISO, RF95_MOSI, RF95_NSS);
    SPI.setFrequency(4000000);
#endif

    // Initialize the screen first so we can show the logo while we start up everything else.
    screen = new graphics::Screen(screen_found);

    readFromRTC(); // read the main CPU RTC at first (in case we can't get GPS time)

    gps = createGps();

    if (gps)
        gpsStatus->observe(&gps->newStatus);
    else
        LOG_WARN("No GPS found - running without GPS\n");

    nodeStatus->observe(&nodeDB.newStatus);

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
#if defined(ST7735_CS) || defined(USE_EINK) || defined(ILI9341_DRIVER)
    screen->setup();
#else
    if (screen_found)
        screen->setup();
#endif

    screen->print("Started...\n");

    // We have now loaded our saved preferences from flash

    // ONCE we will factory reset the GPS for bug #327
    if (gps && !devicestate.did_gps_reset) {
        LOG_WARN("GPS FactoryReset requested\n");
        if (gps->factoryReset()) { // If we don't succeed try again next time
            devicestate.did_gps_reset = true;
            nodeDB.saveToDisk(SEGMENT_DEVICESTATE);
        }
    }

#ifdef SX126X_ANT_SW
    // make analog PA vs not PA switch on SX126x eval board work properly
    pinMode(SX126X_ANT_SW, OUTPUT);
    digitalWrite(SX126X_ANT_SW, 1);
#endif

    // radio init MUST BE AFTER service.init, so we have our radio config settings (from nodedb init)

#if !HAS_RADIO && defined(ARCH_PORTDUINO)
    if (!rIf) {
        rIf = new SimRadio;
        if (!rIf->init()) {
            LOG_WARN("Failed to find simulated radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("Using SIMULATED radio!\n");
        }
    }
#endif

#if defined(RF95_IRQ)
    if (!rIf) {
        rIf = new RF95Interface(RF95_NSS, RF95_IRQ, RF95_RESET, SPI);
        if (!rIf->init()) {
            LOG_WARN("Failed to find RF95 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("RF95 Radio init succeeded, using RF95 radio\n");
        }
    }
#endif

#if defined(USE_SX1280)
    if (!rIf) {
        rIf = new SX1280Interface(SX128X_CS, SX128X_DIO1, SX128X_RESET, SX128X_BUSY, SPI);
        if (!rIf->init()) {
            LOG_WARN("Failed to find SX1280 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1280 Radio init succeeded, using SX1280 radio\n");
        }
    }
#endif

#if defined(USE_SX1262)
    if (!rIf) {
        rIf = new SX1262Interface(SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY, SPI);
        if (!rIf->init()) {
            LOG_WARN("Failed to find SX1262 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1262 Radio init succeeded, using SX1262 radio\n");
        }
    }
#endif

#if defined(USE_SX1268)
    if (!rIf) {
        rIf = new SX1268Interface(SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY, SPI);
        if (!rIf->init()) {
            LOG_WARN("Failed to find SX1268 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1268 Radio init succeeded, using SX1268 radio\n");
        }
    }
#endif

#if defined(USE_LLCC68)
    if (!rIf) {
        rIf = new LLCC68Interface(SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY, SPI);
        if (!rIf->init()) {
            LOG_WARN("Failed to find LLCC68 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("LLCC68 Radio init succeeded, using LLCC68 radio\n");
        }
    }
#endif

    // check if the radio chip matches the selected region

    if ((config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) && (!rIf->wideLora())) {
        LOG_WARN("Radio chip does not support 2.4GHz LoRa. Reverting to unset.\n");
        config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
        nodeDB.saveToDisk(SEGMENT_CONFIG);
        if (!rIf->reconfigure()) {
            LOG_WARN("Reconfigure failed, rebooting\n");
            screen->startRebootScreen();
            rebootAtMsec = millis() + 5000;
        }
    }

#if HAS_WIFI || HAS_ETHERNET
    mqttInit();
#endif

#ifndef ARCH_PORTDUINO
    // Initialize Wifi
    initWifi();

    // Initialize Ethernet
    initEthernet();
#endif

#ifdef ARCH_ESP32
    // Start web server thread.
    webServerThread = new WebServerThread();
#endif

#ifdef ARCH_PORTDUINO
    initApiServer(TCPPort);
#endif

    // Start airtime logger thread.
    airTime = new AirTime();

    if (!rIf)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_NO_RADIO);
    else {
        router->addInterface(rIf);

        // Calculate and save the bit rate to myNodeInfo
        // TODO: This needs to be added what ever method changes the channel from the phone.
        myNodeInfo.bitrate =
            (float(meshtastic_Constants_DATA_PAYLOAD_LEN) / (float(rIf->getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN)))) *
            1000;
        LOG_DEBUG("myNodeInfo.bitrate = %f bytes / sec\n", myNodeInfo.bitrate);
    }

    // This must be _after_ service.init because we need our preferences loaded from flash to have proper timeout values
    PowerFSM_setup(); // we will transition to ON in a couple of seconds, FIXME, only do this for cold boots, not waking from SDS
    powerFSMthread = new PowerFSMThread();

    // setBluetoothEnable(false); we now don't start bluetooth until we enter the proper state
    setCPUFast(false); // 80MHz is fine for our slow peripherals
}

uint32_t rebootAtMsec;   // If not zero we will reboot at this time (used to reboot shortly after the update completes)
uint32_t shutdownAtMsec; // If not zero we will shutdown at this time (used to shutdown from python or mobile client)

// If a thread does something that might need for it to be rescheduled ASAP it can set this flag
// This will supress the current delay and instead try to run ASAP.
bool runASAP;

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
