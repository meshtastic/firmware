#include "configuration.h"
#include "GPS.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "airtime.h"
#include "buzz.h"
#include "error.h"
#include "power.h"
// #include "rom/rtc.h"
#include "DSRRouter.h"
// #include "debug.h"
#include "FSCommon.h"
#include "RTC.h"
#include "SPILock.h"
#include "concurrency/OSThread.h"
#include "concurrency/Periodic.h"
#include "graphics/Screen.h"
#include "main.h"
#include "plugins/Plugins.h"
#include "sleep.h"
#include "target_specific.h"
#include <OneButton.h>
#include <Wire.h>
// #include <driver/rtc_io.h>

#include "mesh/http/WiFiAPClient.h"

#ifndef NO_ESP32
#include "mesh/http/WebServer.h"
#include "nimble/BluetoothUtil.h"
#endif

#if defined(HAS_WIFI) || defined(PORTDUINO)
#include "mesh/wifi/WiFiServerAPI.h"
#include "mqtt/MQTT.h"
#endif

#include "RF95Interface.h"
#include "SX1262Interface.h"
#include "SX1268Interface.h"
#include "LLCC68Interface.h"


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

bool axp192_found;

Router *router = NULL; // Users of router don't care what sort of subclass implements that API

// -----------------------------------------------------------------------------
// Application
// -----------------------------------------------------------------------------
#ifndef NO_WIRE
void scanI2Cdevice(void)
{
    byte err, addr;
    int nDevices = 0;
    for (addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        err = Wire.endTransmission();
        if (err == 0) {
            DEBUG_MSG("I2C device found at address 0x%x\n", addr);

            nDevices++;

            if (addr == SSD1306_ADDRESS) {
                screen_found = addr;
                DEBUG_MSG("ssd1306 display found\n");
            }
            if (addr == ST7567_ADDRESS) {
                screen_found = addr;
                DEBUG_MSG("st7567 display found\n");
            }
#ifdef AXP192_SLAVE_ADDRESS
            if (addr == AXP192_SLAVE_ADDRESS) {
                axp192_found = true;
                DEBUG_MSG("axp192 PMU found\n");
            }
#endif
        } else if (err == 4) {
            DEBUG_MSG("Unknow error at address 0x%x\n", addr);
        }
    }

    if (nDevices == 0)
        DEBUG_MSG("No I2C devices found\n");
    else
        DEBUG_MSG("done\n");
}
#else
void scanI2Cdevice(void) {}
#endif

const char *getDeviceName()
{
    uint8_t dmac[6];

    getMacAddr(dmac);

    // Meshtastic_ab3c
    static char name[20];
    sprintf(name, "Meshtastic_%02x%02x", dmac[4], dmac[5]);
    return name;
}

static int32_t ledBlinker()
{
    static bool ledOn;
    ledOn ^= 1;

    setLed(ledOn);

    // have a very sparse duty cycle of LED being on, unless charging, then blink 0.5Hz square wave rate to indicate that
    return powerStatus->getIsCharging() ? 1000 : (ledOn ? 1 : 1000);
}

uint32_t timeLastPowered = 0;

/// Wrapper to convert our powerFSM stuff into a 'thread'
class PowerFSMThread : public OSThread
{
  public:
    // callback returns the period for the next callback invocation (or 0 if we should no longer be called)
    PowerFSMThread() : OSThread("PowerFSM") {}

  protected:
    int32_t runOnce() override
    {
        powerFSM.run_machine();

        /// If we are in power state we force the CPU to wake every 10ms to check for serial characters (we don't yet wake
        /// cpu for serial rx - FIXME)
        auto state = powerFSM.getState();
        canSleep = (state != &statePOWER) && (state != &stateSERIAL);

        if (powerStatus->getHasUSB()) {
            timeLastPowered = millis();
        } else if (radioConfig.preferences.on_battery_shutdown_after_secs > 0 && 
                    millis() > timeLastPowered + (1000 * radioConfig.preferences.on_battery_shutdown_after_secs)) { //shutdown after 30 minutes unpowered
            powerFSM.trigger(EVENT_SHUTDOWN);
        }
        
        return 10;
    }
};

/**
 * Watch a GPIO and if we get an IRQ, wake the main thread.
 * Use to add wake on button press
 */
void wakeOnIrq(int irq, int mode)
{
    attachInterrupt(
        irq,
        [] {
            BaseType_t higherWake = 0;
            mainDelay.interruptFromISR(&higherWake);
        },
        FALLING);
}

class ButtonThread : public OSThread
{
// Prepare for button presses
#ifdef BUTTON_PIN
    OneButton userButton;
#endif
#ifdef BUTTON_PIN_ALT
    OneButton userButtonAlt;
#endif
#ifdef BUTTON_PIN_TOUCH
    OneButton userButtonTouch;
#endif
    static bool shutdown_on_long_stop;

  public:
    static uint32_t longPressTime;

    // callback returns the period for the next callback invocation (or 0 if we should no longer be called)
    ButtonThread() : OSThread("Button")
    {
#ifdef BUTTON_PIN
        userButton = OneButton(BUTTON_PIN, true, true);
#ifdef INPUT_PULLUP_SENSE
        // Some platforms (nrf52) have a SENSE variant which allows wake from sleep - override what OneButton did
        pinMode(BUTTON_PIN, INPUT_PULLUP_SENSE);
#endif
        userButton.attachClick(userButtonPressed);
        userButton.attachDuringLongPress(userButtonPressedLong);
        userButton.attachDoubleClick(userButtonDoublePressed);
        userButton.attachMultiClick(userButtonMultiPressed);
        userButton.attachLongPressStart(userButtonPressedLongStart);
        userButton.attachLongPressStop(userButtonPressedLongStop);
        wakeOnIrq(BUTTON_PIN, FALLING);
#endif
#ifdef BUTTON_PIN_ALT
        userButtonAlt = OneButton(BUTTON_PIN_ALT, true, true);
#ifdef INPUT_PULLUP_SENSE
        // Some platforms (nrf52) have a SENSE variant which allows wake from sleep - override what OneButton did
        pinMode(BUTTON_PIN_ALT, INPUT_PULLUP_SENSE);
#endif
        userButtonAlt.attachClick(userButtonPressed);
        userButtonAlt.attachDuringLongPress(userButtonPressedLong);
        userButtonAlt.attachDoubleClick(userButtonDoublePressed);
        userButtonAlt.attachLongPressStart(userButtonPressedLongStart);
        userButtonAlt.attachLongPressStop(userButtonPressedLongStop);
        wakeOnIrq(BUTTON_PIN_ALT, FALLING);
#endif

#ifdef BUTTON_PIN_TOUCH
        userButtonTouch = OneButton(BUTTON_PIN_TOUCH, true, true);
        userButtonTouch.attachClick(touchPressed);
        userButtonTouch.attachDuringLongPress(touchPressedLong);
        userButtonTouch.attachDoubleClick(touchDoublePressed);
        userButtonTouch.attachLongPressStart(touchPressedLongStart);
        userButtonTouch.attachLongPressStop(touchPressedLongStop);
        wakeOnIrq(BUTTON_PIN_TOUCH, FALLING);
#endif

    }

  protected:
    /// If the button is pressed we suppress CPU sleep until release
    int32_t runOnce() override
    {
        canSleep = true; // Assume we should not keep the board awake

#ifdef BUTTON_PIN
        userButton.tick();
        canSleep &= userButton.isIdle();
#endif
#ifdef BUTTON_PIN_ALT
        userButtonAlt.tick();
        canSleep &= userButtonAlt.isIdle();
#endif
#ifdef BUTTON_PIN_TOUCH
        userButtonTouch.tick();
        canSleep &= userButtonTouch.isIdle();
#endif
        // if (!canSleep) DEBUG_MSG("Supressing sleep!\n");
        // else DEBUG_MSG("sleep ok\n");

        return 5;
    }

  private:
    static void touchPressed()
    {        
        screen->forceDisplay();
        DEBUG_MSG("touch press!\n");       
    }
    static void touchDoublePressed()
    {
        DEBUG_MSG("touch double press!\n");       
    }
    static void touchPressedLong()
    {
        DEBUG_MSG("touch press long!\n");       
    }
    static void touchDoublePressedLong()
    {
        DEBUG_MSG("touch double pressed!\n");       
    }
    static void touchPressedLongStart()
    {        
        DEBUG_MSG("touch long press start!\n");       
    }
    static void touchPressedLongStop()
    {       
        DEBUG_MSG("touch long press stop!\n");       
    }


    static void userButtonPressed()
    {
        // DEBUG_MSG("press!\n");
#ifdef BUTTON_PIN
        if ((BUTTON_PIN != radioConfig.preferences.rotary1_pin_press) || !radioConfig.preferences.canned_message_plugin_enabled) {
            powerFSM.trigger(EVENT_PRESS);
        }
#endif
    }
    static void userButtonPressedLong()
    {
        // DEBUG_MSG("Long press!\n");
#ifndef NRF52_SERIES
        screen->adjustBrightness();
#endif
        // If user button is held down for 5 seconds, shutdown the device.
        if (millis() - longPressTime > 5 * 1000) {
#ifdef TBEAM_V10
            if (axp192_found == true) {
                setLed(false);
                power->shutdown();
            }
#elif NRF52_SERIES
            // Do actual shutdown when button released, otherwise the button release
            // may wake the board immediatedly.
            if (!shutdown_on_long_stop) {
                screen->startShutdownScreen();
                DEBUG_MSG("Shutdown from long press");
                playBeep();
                ledOff(PIN_LED1);
                ledOff(PIN_LED2);
                shutdown_on_long_stop = true;
            }
#endif
        } else {
            // DEBUG_MSG("Long press %u\n", (millis() - longPressTime));
        }
    }

    static void userButtonDoublePressed()
    {
#ifndef NO_ESP32
        disablePin();
#elif defined(HAS_EINK)
        digitalWrite(PIN_EINK_EN,digitalRead(PIN_EINK_EN) == LOW);
#endif
    }

    static void userButtonMultiPressed()
    {
#ifndef NO_ESP32
        clearNVS();
#endif
#ifdef NRF52_SERIES
        clearBonds();
#endif
    }


    static void userButtonPressedLongStart()
    {
        DEBUG_MSG("Long press start!\n");
        longPressTime = millis();
    }

    static void userButtonPressedLongStop()
    {
        DEBUG_MSG("Long press stop!\n");
        longPressTime = 0;
        if (shutdown_on_long_stop) {
            playShutdownMelody();
            delay(3000);
            power->shutdown();
        }
    }
};

bool ButtonThread::shutdown_on_long_stop = false;

static Periodic *ledPeriodic;
static OSThread *powerFSMthread, *buttonThread;
uint32_t ButtonThread::longPressTime = 0;

RadioInterface *rIf = NULL;

/**
 * Some platforms (nrf52) might provide an alterate version that supresses calling delay from sleep.
 */
__attribute__ ((weak, noinline)) bool loopCanSleep() {
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
    if (!radioConfig.preferences.serial_disabled) {
        consoleInit(); // Set serial baud rate and init our mesh console
    }
#endif

    DEBUG_MSG("\n\n//\\ E S H T /\\ S T / C\n\n");

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

    bool forceSoftAP = 0;

#ifdef BUTTON_PIN
#ifndef NO_ESP32

    // If the button is connected to GPIO 12, don't enable the ability to use
    // meshtasticAdmin on the device.
    pinMode(BUTTON_PIN, INPUT);

#ifdef BUTTON_NEED_PULLUP
    gpio_pullup_en((gpio_num_t)BUTTON_PIN);
    delay(10);
#endif

    // BUTTON_PIN is pulled high by a 12k resistor.
    if (!digitalRead(BUTTON_PIN)) {
        forceSoftAP = 1;
        DEBUG_MSG("Setting forceSoftAP = 1\n");
    }

#endif
#endif

    OSThread::setup();

    ledPeriodic = new Periodic("Blink", ledBlinker);

    fsInit();

    router = new DSRRouter();

#ifdef I2C_SDA
    Wire.begin(I2C_SDA, I2C_SCL);
#elif !defined(NO_WIRE)
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

    scanI2Cdevice();

    // Buttons & LED
    buttonThread = new ButtonThread();

#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, 1 ^ LED_INVERTED); // turn on for now
#endif

    // Hello
    DEBUG_MSG("Meshtastic hwvendor=%d, swver=%s, hwver=%s\n", HW_VENDOR, optstr(APP_VERSION), optstr(HW_VERSION));

#ifndef NO_ESP32
    // Don't init display if we don't have one or we are waking headless due to a timer event
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER)
        screen_found = 0; // forget we even have the hardware

    esp32Setup();
#endif

#ifdef NRF52_SERIES
    nrf52Setup();
#endif
    playStartMelody();
    // We do this as early as possible because this loads preferences from flash
    // but we need to do this after main cpu iniot (esp32setup), because we need the random seed set
    nodeDB.init();

    // Currently only the tbeam has a PMU
    power = new Power();
    power->setStatusHandler(powerStatus);
    powerStatus->observe(&power->newStatus);
    power->setup(); // Must be after status handler is installed, so that handler gets notified of the initial configuration

    // Init our SPI controller (must be before screen and lora)
    initSPI();
#ifdef NO_ESP32
    SPI.begin();
#else
    // ESP32
    SPI.begin(RF95_SCK, RF95_MISO, RF95_MOSI, RF95_NSS);
    SPI.setFrequency(4000000);
#endif

    // Initialize the screen first so we can show the logo while we start up everything else.
    screen = new graphics::Screen(screen_found);

    readFromRTC(); // read the main CPU RTC at first (in case we can't get GPS time)

#ifdef GENIEBLOCKS
    Im intentionally breaking your build so you see this note.Feel free to revert if not correct.I think you can
            remove this GPS_RESET_N code by instead defining PIN_GPS_RESET and
        use the shared code in GPS.cpp instead.- geeksville

                                                     // gps setup
                                                     pinMode(GPS_RESET_N, OUTPUT);
    pinMode(GPS_EXTINT, OUTPUT);
    digitalWrite(GPS_RESET_N, HIGH);
    digitalWrite(GPS_EXTINT, LOW);
    // battery setup
    // If we want to read battery level, we need to set BATTERY_EN_PIN pin to low.
    // ToDo: For low power consumption after read battery level, set that pin to high.
    pinMode(BATTERY_EN_PIN, OUTPUT);
    digitalWrite(BATTERY_EN_PIN, LOW);
#endif
    gps = createGps();

    if (gps)
        gpsStatus->observe(&gps->newStatus);
    else
        DEBUG_MSG("Warning: No GPS found - running without GPS\n");

    nodeStatus->observe(&nodeDB.newStatus);

    service.init();

    // Now that the mesh service is created, create any plugins
    setupPlugins();

    // Do this after service.init (because that clears error_code)
#ifdef AXP192_SLAVE_ADDRESS
    if (!axp192_found)
        RECORD_CRITICALERROR(CriticalErrorCode_NoAXP192); // Record a hardware fault for missing hardware
#endif

        // Don't call screen setup until after nodedb is setup (because we need
        // the current region name)
#if defined(ST7735_CS) || defined(HAS_EINK)
    screen->setup();
#else
    if (screen_found)
        screen->setup();
#endif

    screen->print("Started...\n");

    // We have now loaded our saved preferences from flash

    // ONCE we will factory reset the GPS for bug #327
    if (gps && !devicestate.did_gps_reset) {
        DEBUG_MSG("GPS FactoryReset requested\n");
        if (gps->factoryReset()) { // If we don't succeed try again next time
            devicestate.did_gps_reset = true;
            nodeDB.saveToDisk();
        }
    }

#ifdef SX126X_ANT_SW
    // make analog PA vs not PA switch on SX126x eval board work properly
    pinMode(SX126X_ANT_SW, OUTPUT);
    digitalWrite(SX126X_ANT_SW, 1);
#endif

    // radio init MUST BE AFTER service.init, so we have our radio config settings (from nodedb init)

#if defined(RF95_IRQ)
    if (!rIf) {
        rIf = new RF95Interface(RF95_NSS, RF95_IRQ, RF95_RESET, SPI);
        if (!rIf->init()) {
            DEBUG_MSG("Warning: Failed to find RF95 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            DEBUG_MSG("RF95 Radio init succeeded, using RF95 radio\n");
        }
    }
#endif

#if defined(USE_SX1262)
    if (!rIf) {
        rIf = new SX1262Interface(SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY, SPI);
        if (!rIf->init()) {
            DEBUG_MSG("Warning: Failed to find SX1262 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            DEBUG_MSG("SX1262 Radio init succeeded, using SX1262 radio\n");
        }
    }
#endif

#if defined(USE_SX1268)
    if (!rIf) {
        rIf = new SX1268Interface(SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY, SPI);
        if (!rIf->init()) {
            DEBUG_MSG("Warning: Failed to find SX1268 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            DEBUG_MSG("SX1268 Radio init succeeded, using SX1268 radio\n");
        }
    }
#endif

#if defined(USE_LLCC68)
    if (!rIf) {
        rIf = new LLCC68Interface(SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY, SPI);
        if (!rIf->init()) {
            DEBUG_MSG("Warning: Failed to find LLCC68 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            DEBUG_MSG("LLCC68 Radio init succeeded, using LLCC68 radio\n");
        }
    }
#endif

#ifdef USE_SIM_RADIO
    if (!rIf) {
        rIf = new SimRadio;
        if (!rIf->init()) {
            DEBUG_MSG("Warning: Failed to find simulated radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            DEBUG_MSG("Using SIMULATED radio!\n");
        }
    }
#endif

#if defined(PORTDUINO) || defined(HAS_WIFI)
    mqttInit();
#endif

    // Initialize Wifi
    initWifi(forceSoftAP);

#ifndef NO_ESP32
    // Start web server thread.
    webServerThread = new WebServerThread();
#endif

#ifdef PORTDUINO
    initApiServer();
#endif

    // Start airtime logger thread.
    airTime = new AirTime();

    if (!rIf)
        RECORD_CRITICALERROR(CriticalErrorCode_NoRadio);
    else{
        router->addInterface(rIf);

        // Calculate and save the bit rate to myNodeInfo
        // TODO: This needs to be added what ever method changes the channel from the phone.
        myNodeInfo.bitrate = (float(Constants_DATA_PAYLOAD_LEN) /
                        (float(rIf->getPacketTime(Constants_DATA_PAYLOAD_LEN)))
                        ) * 1000;
        DEBUG_MSG("myNodeInfo.bitrate = %f bytes / sec\n", myNodeInfo.bitrate);
    }

    // This must be _after_ service.init because we need our preferences loaded from flash to have proper timeout values
    PowerFSM_setup(); // we will transition to ON in a couple of seconds, FIXME, only do this for cold boots, not waking from SDS
    powerFSMthread = new PowerFSMThread();

    // setBluetoothEnable(false); we now don't start bluetooth until we enter the proper state
    setCPUFast(false); // 80MHz is fine for our slow peripherals
}

#if 0
// Turn off for now

uint32_t axpDebugRead()
{
  axp.debugCharging();
  DEBUG_MSG("vbus current %f\n", axp.getVbusCurrent());
  DEBUG_MSG("charge current %f\n", axp.getBattChargeCurrent());
  DEBUG_MSG("bat voltage %f\n", axp.getBattVoltage());
  DEBUG_MSG("batt pct %d\n", axp.getBattPercentage());
  DEBUG_MSG("is battery connected %d\n", axp.isBatteryConnect());
  DEBUG_MSG("is USB connected %d\n", axp.isVBUSPlug());
  DEBUG_MSG("is charging %d\n", axp.isChargeing());

  return 30 * 1000;
}

Periodic axpDebugOutput(axpDebugRead);
axpDebugOutput.setup();
#endif

uint32_t rebootAtMsec; // If not zero we will reboot at this time (used to reboot shortly after the update completes)
uint32_t shutdownAtMsec; // If not zero we will shutdown at this time (used to shutdown from python or mobile client)

void powerCommandsCheck()
{
    if (rebootAtMsec && millis() > rebootAtMsec) {
#ifndef NO_ESP32
        DEBUG_MSG("Rebooting for update\n");
        ESP.restart();
#elif NRF52_SERIES
    NVIC_SystemReset();
#else
        DEBUG_MSG("FIXME implement reboot for this platform");
#endif
    }

#if NRF52_SERIES
    if (shutdownAtMsec) {
        screen->startShutdownScreen();
        playBeep();
        ledOff(PIN_LED1);
        ledOff(PIN_LED2);
    }
#endif

    if (shutdownAtMsec && millis() > shutdownAtMsec) {
        DEBUG_MSG("Shutting down from admin command\n");
#ifdef TBEAM_V10
        if (axp192_found == true) {
            setLed(false);
            power->shutdown();
        }
#elif NRF52_SERIES
        playShutdownMelody();
        power->shutdown();
#else
        DEBUG_MSG("FIXME implement shutdown for this platform");
#endif
    }
}

// If a thread does something that might need for it to be rescheduled ASAP it can set this flag
// This will supress the current delay and instead try to run ASAP.
bool runASAP;

void loop()
{
    runASAP = false;

    // axpDebugOutput.loop();

    // heap_caps_check_integrity_all(true); // FIXME - disable this expensive check

#ifndef NO_ESP32
    esp32Loop();
#endif
#ifdef NRF52_SERIES
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
        DEBUG_MSG("Next %s in %ld\n", mainController.nextThread->ThreadName.c_str(),
                  mainController.nextThread->tillRun(millis())); */

    // We want to sleep as long as possible here - because it saves power
    if (!runASAP && loopCanSleep()) {
        // if(delayMsec > 100) DEBUG_MSG("sleeping %ld\n", delayMsec);
        mainDelay.delay(delayMsec);
    }
    // if (didWake) DEBUG_MSG("wake!\n");
}
