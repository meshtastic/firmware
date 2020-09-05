/*

  Main module

  # Modified by Kyle T. Gabriel to fix issue with incorrect GPS data for TTNMapper

  Copyright (C) 2018 by Xose Pérez <xose dot perez at gmail dot com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "MeshRadio.h"
#include "MeshService.h"
#include "NEMAGPS.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "UBloxGPS.h"
#include "concurrency/Periodic.h"
#include "configuration.h"
#include "error.h"
#include "power.h"
// #include "rom/rtc.h"
#include "DSRRouter.h"
// #include "debug.h"
#include "SPILock.h"
#include "graphics/Screen.h"
#include "main.h"
#include "sleep.h"
#include "timing.h"
#include <OneButton.h>
#include <Wire.h>
// #include <driver/rtc_io.h>

#ifndef NO_ESP32
#include "nimble/BluetoothUtil.h"
#endif

#include "RF95Interface.h"
#include "SX1262Interface.h"

#ifdef NRF52_SERIES
#include "variant.h"
#endif

// We always create a screen object, but we only init it if we find the hardware
graphics::Screen screen(SSD1306_ADDRESS);

// Global power status
meshtastic::PowerStatus *powerStatus = new meshtastic::PowerStatus();

// Global GPS status
meshtastic::GPSStatus *gpsStatus = new meshtastic::GPSStatus();

// Global Node status
meshtastic::NodeStatus *nodeStatus = new meshtastic::NodeStatus();

bool ssd1306_found;
bool axp192_found;

DSRRouter realRouter;
Router &router = realRouter; // Users of router don't care what sort of subclass implements that API

// -----------------------------------------------------------------------------
// Application
// -----------------------------------------------------------------------------

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
                ssd1306_found = true;
                DEBUG_MSG("ssd1306 display found\n");
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

const char *getDeviceName()
{
    uint8_t dmac[6];

    getMacAddr(dmac);

    // Meshtastic_ab3c
    static char name[20];
    sprintf(name, "Meshtastic_%02x%02x", dmac[4], dmac[5]);
    return name;
}

static uint32_t ledBlinker()
{
    static bool ledOn;
    ledOn ^= 1;

    setLed(ledOn);

    // have a very sparse duty cycle of LED being on, unless charging, then blink 0.5Hz square wave rate to indicate that
    return powerStatus->getIsCharging() ? 1000 : (ledOn ? 2 : 1000);
}

concurrency::Periodic ledPeriodic(ledBlinker);

// Prepare for button presses
#ifdef BUTTON_PIN
OneButton userButton;
#endif
#ifdef BUTTON_PIN_ALT
OneButton userButtonAlt;
#endif
void userButtonPressed()
{
    powerFSM.trigger(EVENT_PRESS);
}
void userButtonPressedLong()
{
    screen.adjustBrightness();
}

void setup()
{
#ifdef USE_SEGGER
    SEGGER_RTT_ConfigUpBuffer(0, NULL, NULL, 0, SEGGER_RTT_MODE_NO_BLOCK_TRIM);
#endif

// Debug
#ifdef DEBUG_PORT
    DEBUG_PORT.init(); // Set serial baud rate and init our mesh console
#endif

    initDeepSleep();

#ifdef VEXT_ENABLE
    pinMode(VEXT_ENABLE, OUTPUT);
    digitalWrite(VEXT_ENABLE, 0); // turn on the display power
#endif

#ifdef RESET_OLED
    pinMode(RESET_OLED, OUTPUT);
    digitalWrite(RESET_OLED, 1);
#endif

#ifdef I2C_SDA
    Wire.begin(I2C_SDA, I2C_SCL);
#else
    Wire.begin();
#endif
    // i2c still busted on new board
#ifndef ARDUINO_NRF52840_PPR
    scanI2Cdevice();
#endif

    // Buttons & LED
#ifdef BUTTON_PIN
    userButton = OneButton(BUTTON_PIN, true, true);
    userButton.attachClick(userButtonPressed);
    userButton.attachDuringLongPress(userButtonPressedLong);
#endif
#ifdef BUTTON_PIN_ALT
    userButtonAlt = OneButton(BUTTON_PIN_ALT, true, true);
    userButtonAlt.attachClick(userButtonPressed);
    userButton.attachDuringLongPress(userButtonPressedLong);
#endif
#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, 1 ^ LED_INVERTED); // turn on for now
#endif

    ledPeriodic.setup();

    // Hello
    DEBUG_MSG("Meshtastic swver=%s, hwver=%s\n", optstr(APP_VERSION), optstr(HW_VERSION));

#ifndef NO_ESP32
    // Don't init display if we don't have one or we are waking headless due to a timer event
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER)
        ssd1306_found = false; // forget we even have the hardware

    esp32Setup();
#endif

    // Currently only the tbeam has a PMU
    power = new Power();
    power->setup();
    power->setStatusHandler(powerStatus);
    powerStatus->observe(&power->newStatus);

#ifdef NRF52_SERIES
    nrf52Setup();
#endif

    // Init our SPI controller (must be before screen and lora)
    initSPI();
#ifdef NRF52_SERIES
    SPI.begin();
#else
    // ESP32
    SPI.begin(RF95_SCK, RF95_MISO, RF95_MOSI, RF95_NSS);
    SPI.setFrequency(4000000);
#endif

    // Initialize the screen first so we can show the logo while we start up everything else.
#ifdef ST7735_CS
    screen.setup();
#else
    if (ssd1306_found)
        screen.setup();
#endif

    screen.print("Started...\n");

    readFromRTC(); // read the main CPU RTC at first (in case we can't get GPS time)

// If we know we have a L80 GPS, don't try UBLOX
#ifndef L80_RESET
    // Init GPS - first try ublox
    auto ublox = new UBloxGPS();
    gps = ublox;
    if (!gps->setup()) {
        DEBUG_MSG("ERROR: No UBLOX GPS found\n");

        delete ublox;
        gps = ublox = NULL;

        if (GPS::_serial_gps) {
            // Some boards might have only the TX line from the GPS connected, in that case, we can't configure it at all.  Just
            // assume NEMA at 9600 baud.
            DEBUG_MSG("Hoping that NEMA might work\n");

            // dumb NEMA access only work for serial GPSes)
            gps = new NEMAGPS();
            gps->setup();
        }
    }
#else
    gps = new NEMAGPS();
    gps->setup();
#endif
    gpsStatus->observe(&gps->newStatus);
    nodeStatus->observe(&nodeDB.newStatus);

    service.init();

    // We have now loaded our saved preferences from flash

    // ONCE we will factory reset the GPS for bug #327
    if (ublox && !devicestate.did_gps_reset) {
        if (ublox->factoryReset()) { // If we don't succeed try again next time
            devicestate.did_gps_reset = true;
            nodeDB.saveToDisk();
        }
    }

#ifdef SX1262_ANT_SW
    // make analog PA vs not PA switch on SX1262 eval board work properly
    pinMode(SX1262_ANT_SW, OUTPUT);
    digitalWrite(SX1262_ANT_SW, 1);
#endif

    // MUST BE AFTER service.init, so we have our radio config settings (from nodedb init)
    RadioInterface *rIf = NULL;

#if defined(RF95_IRQ)
    if (!rIf) {
        rIf = new RF95Interface(RF95_NSS, RF95_IRQ, RF95_RESET, SPI);
        if (!rIf->init()) {
            DEBUG_MSG("Warning: Failed to find RF95 radio\n");
            delete rIf;
            rIf = NULL;
        }
    }
#endif

#if defined(SX1262_CS)
    if (!rIf) {
        rIf = new SX1262Interface(SX1262_CS, SX1262_DIO1, SX1262_RESET, SX1262_BUSY, SPI);
        if (!rIf->init()) {
            DEBUG_MSG("Warning: Failed to find SX1262 radio\n");
            delete rIf;
            rIf = NULL;
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
        }
    }
#endif

    if (!rIf)
        recordCriticalError(ErrNoRadio);
    else
        router.addInterface(rIf);

    // This must be _after_ service.init because we need our preferences loaded from flash to have proper timeout values
    PowerFSM_setup(); // we will transition to ON in a couple of seconds, FIXME, only do this for cold boots, not waking from SDS

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

concurrency::Periodic axpDebugOutput(axpDebugRead);
axpDebugOutput.setup();
#endif

void loop()
{
    uint32_t msecstosleep = 1000 * 30; // How long can we sleep before we again need to service the main loop?

    gps->loop(); // FIXME, remove from main, instead block on read
    router.loop();
    powerFSM.run_machine();
    service.loop();

    concurrency::periodicScheduler.loop();
    // axpDebugOutput.loop();

#ifdef DEBUG_PORT
    DEBUG_PORT.loop(); // Send/receive protobufs over the serial port
#endif

    // heap_caps_check_integrity_all(true); // FIXME - disable this expensive check

#ifndef NO_ESP32
    esp32Loop();
#endif
#ifdef TBEAM_V10
    power->loop();
#endif

#ifdef BUTTON_PIN
    userButton.tick();
#endif
#ifdef BUTTON_PIN_ALT
    userButtonAlt.tick();
#endif

    // Show boot screen for first 3 seconds, then switch to normal operation.
    static bool showingBootScreen = true;
    if (showingBootScreen && (timing::millis() > 3000)) {
        screen.stopBootScreen();
        showingBootScreen = false;
    }

#ifdef DEBUG_STACK
    static uint32_t lastPrint = 0;
    if (timing::millis() - lastPrint > 10 * 1000L) {
        lastPrint = timing::millis();
        meshtastic::printThreadInfo("main");
    }
#endif

    // Update the screen last, after we've figured out what to show.
    screen.debug_info()->setChannelNameStatus(getChannelName());
    
    // No GPS lock yet, let the OS put the main CPU in low power mode for 100ms (or until another interrupt comes in)
    // i.e. don't just keep spinning in loop as fast as we can.
    // DEBUG_MSG("msecs %d\n", msecstosleep);

    // FIXME - until button press handling is done by interrupt (see polling above) we can't sleep very long at all or buttons
    // feel slow
    msecstosleep = 10;

    delay(msecstosleep);
}
