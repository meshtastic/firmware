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

#include "GPS.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Periodic.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "error.h"
#include "power.h"
// #include "rom/rtc.h"
#include "FloodingRouter.h"
#include "screen.h"
#include "sleep.h"
#include <Wire.h>
// #include <driver/rtc_io.h>

#ifndef NO_ESP32
#include "BluetoothUtil.h"
#endif

#ifdef TBEAM_V10
#include "axp20x.h"
AXP20X_Class axp;
bool pmu_irq = false;
#endif

// Global Screen singleton
#ifdef I2C_SDA
meshtastic::Screen screen(SSD1306_ADDRESS, I2C_SDA, I2C_SCL);
#else
// Fake values for pins to keep build happy, we won't ever initialize it.
meshtastic::Screen screen(SSD1306_ADDRESS, 0, 0);
#endif

// Global power status singleton
static meshtastic::PowerStatus powerStatus;

bool ssd1306_found;
bool axp192_found;

FloodingRouter realRouter;
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
#ifdef TBEAM_V10
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

#ifdef TBEAM_V10
/// Reads power status to powerStatus singleton.
//
// TODO(girts): move this and other axp stuff to power.h/power.cpp.
void readPowerStatus()
{
    powerStatus.haveBattery = axp.isBatteryConnect();
    if (powerStatus.haveBattery) {
        powerStatus.batteryVoltageMv = axp.getBattVoltage();
    }
    powerStatus.usb = axp.isVBUSPlug();
    powerStatus.charging = axp.isChargeing();
}
#endif // TBEAM_V10

/**
 * Init the power manager chip
 *
 * axp192 power
    DCDC1 0.7-3.5V @ 1200mA max -> OLED // If you turn this off you'll lose comms to the axp192 because the OLED and the axp192
 share the same i2c bus, instead use ssd1306 sleep mode DCDC2 -> unused DCDC3 0.7-3.5V @ 700mA max -> ESP32 (keep this on!) LDO1
 30mA -> charges GPS backup battery // charges the tiny J13 battery by the GPS to power the GPS ram (for a couple of days), can
 not be turned off LDO2 200mA -> LORA LDO3 200mA -> GPS
 */
void axp192Init()
{
#ifdef TBEAM_V10
    if (axp192_found) {
        if (!axp.begin(Wire, AXP192_SLAVE_ADDRESS)) {
            DEBUG_MSG("AXP192 Begin PASS\n");

            // axp.setChgLEDMode(LED_BLINK_4HZ);
            DEBUG_MSG("DCDC1: %s\n", axp.isDCDC1Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("DCDC2: %s\n", axp.isDCDC2Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("LDO2: %s\n", axp.isLDO2Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("LDO3: %s\n", axp.isLDO3Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("DCDC3: %s\n", axp.isDCDC3Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("Exten: %s\n", axp.isExtenEnable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("----------------------------------------\n");

            axp.setPowerOutPut(AXP192_LDO2, AXP202_ON); // LORA radio
            axp.setPowerOutPut(AXP192_LDO3, AXP202_ON); // GPS main power
            axp.setPowerOutPut(AXP192_DCDC2, AXP202_ON);
            axp.setPowerOutPut(AXP192_EXTEN, AXP202_ON);
            axp.setPowerOutPut(AXP192_DCDC1, AXP202_ON);
            axp.setDCDC1Voltage(3300); // for the OLED power

            DEBUG_MSG("DCDC1: %s\n", axp.isDCDC1Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("DCDC2: %s\n", axp.isDCDC2Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("LDO2: %s\n", axp.isLDO2Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("LDO3: %s\n", axp.isLDO3Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("DCDC3: %s\n", axp.isDCDC3Enable() ? "ENABLE" : "DISABLE");
            DEBUG_MSG("Exten: %s\n", axp.isExtenEnable() ? "ENABLE" : "DISABLE");

#if 0
      // cribbing from https://github.com/m5stack/M5StickC/blob/master/src/AXP192.cpp to fix charger to be more like 300ms.  
      // I finally found an english datasheet.  Will look at this later - but suffice it to say the default code from TTGO has 'issues'

      axp.adc1Enable(0xff, 1); // turn on all adcs
      uint8_t val = 0xc2;
      axp._writeByte(0x33, 1, &val); // Bat charge voltage to 4.2, Current 280mA
      val = 0b11110010;
      // Set ADC sample rate to 200hz
      // axp._writeByte(0x84, 1, &val);

      // Not connected
      //val = 0xfc;
      //axp._writeByte(AXP202_VHTF_CHGSET, 1, &val); // Set temperature protection

      //not used
      //val = 0x46;
      //axp._writeByte(AXP202_OFF_CTL, 1, &val); // enable bat detection
#endif
            axp.debugCharging();

#ifdef PMU_IRQ
            pinMode(PMU_IRQ, INPUT);
            attachInterrupt(
                PMU_IRQ, [] { pmu_irq = true; }, FALLING);

            axp.adc1Enable(AXP202_BATT_CUR_ADC1, 1);
            axp.enableIRQ(AXP202_BATT_REMOVED_IRQ | AXP202_BATT_CONNECT_IRQ | AXP202_CHARGING_FINISHED_IRQ | AXP202_CHARGING_IRQ |
                              AXP202_VBUS_REMOVED_IRQ | AXP202_VBUS_CONNECT_IRQ | AXP202_PEK_SHORTPRESS_IRQ,
                          1);

            axp.clearIRQ();
#endif
            readPowerStatus();
        } else {
            DEBUG_MSG("AXP192 Begin FAIL\n");
        }
    } else {
        DEBUG_MSG("AXP192 not found\n");
    }
#endif
}

void getMacAddr(uint8_t *dmac)
{
#ifndef NO_ESP32
    assert(esp_efuse_mac_get_default(dmac) == ESP_OK);
#else
    dmac[0] = 0xde;
    dmac[1] = 0xad;
    dmac[2] = 0xbe;
    dmac[3] = 0xef;
    dmac[4] = 0x01;
    dmac[5] = 0x02; // FIXME, macaddr stuff needed for NRF52
#endif
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

static MeshRadio *radio = NULL;

#include "Router.h"

void setup()
{
// Debug
#ifdef DEBUG_PORT
    DEBUG_PORT.begin(SERIAL_BAUD);
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
    scanI2Cdevice();
#endif

    // Buttons & LED
#ifdef BUTTON_PIN
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    digitalWrite(BUTTON_PIN, 1);
#endif
#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, 1 ^ LED_INVERTED); // turn on for now
#endif

    // Hello
    DEBUG_MSG("Meshtastic swver=%s, hwver=%s\n", xstr(APP_VERSION), xstr(HW_VERSION));

#ifndef NO_ESP32
    // Don't init display if we don't have one or we are waking headless due to a timer event
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER)
        ssd1306_found = false; // forget we even have the hardware
#endif

    // Initialize the screen first so we can show the logo while we start up everything else.
    if (ssd1306_found)
        screen.setup();

    axp192Init();

    screen.print("Started...\n");

    // Init GPS
    gps.setup();

    service.init();

#ifndef NO_ESP32
    // MUST BE AFTER service.init, so we have our radio config settings (from nodedb init)
    radio = new MeshRadio();
    router.addInterface(&radio->radioIf);
#endif

    if (radio && !radio->init())
        recordCriticalError(ErrNoRadio);

    // This must be _after_ service.init because we need our preferences loaded from flash to have proper timeout values
    PowerFSM_setup(); // we will transition to ON in a couple of seconds, FIXME, only do this for cold boots, not waking from SDS

    // setBluetoothEnable(false); we now don't start bluetooth until we enter the proper state
    setCPUFast(false); // 80MHz is fine for our slow peripherals
}

uint32_t ledBlinker()
{
    static bool ledOn;
    ledOn ^= 1;

    setLed(ledOn);

    // have a very sparse duty cycle of LED being on, unless charging, then blink 0.5Hz square wave rate to indicate that
    return powerStatus.charging ? 1000 : (ledOn ? 2 : 1000);
}

Periodic ledPeriodic(ledBlinker);

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
#endif

void loop()
{
    uint32_t msecstosleep = 1000 * 30; // How long can we sleep before we again need to service the main loop?

    gps.loop();
    router.loop();
    powerFSM.run_machine();
    service.loop();

    ledPeriodic.loop();
    // axpDebugOutput.loop();

#ifndef NO_ESP32
    loopBLE();
#endif

    // for debug printing
    // radio.radioIf.canSleep();

#ifdef PMU_IRQ
    if (pmu_irq) {
        pmu_irq = false;
        axp.readIRQ();

        DEBUG_MSG("pmu irq!\n");

        if (axp.isChargingIRQ()) {
            DEBUG_MSG("Battery start charging\n");
        }
        if (axp.isChargingDoneIRQ()) {
            DEBUG_MSG("Battery fully charged\n");
        }
        if (axp.isVbusRemoveIRQ()) {
            DEBUG_MSG("USB unplugged\n");
        }
        if (axp.isVbusPlugInIRQ()) {
            DEBUG_MSG("USB plugged In\n");
        }
        if (axp.isBattPlugInIRQ()) {
            DEBUG_MSG("Battery inserted\n");
        }
        if (axp.isBattRemoveIRQ()) {
            DEBUG_MSG("Battery removed\n");
        }
        if (axp.isPEKShortPressIRQ()) {
            DEBUG_MSG("PEK short button press\n");
        }

        readPowerStatus();
        axp.clearIRQ();
    }
#endif // T_BEAM_V10

#ifdef BUTTON_PIN
    // if user presses button for more than 3 secs, discard our network prefs and reboot (FIXME, use a debounce lib instead of
    // this boilerplate)
    static bool wasPressed = false;

    if (!digitalRead(BUTTON_PIN)) {
        if (!wasPressed) { // just started a new press
            DEBUG_MSG("pressing\n");

            // doLightSleep();
            // esp_pm_dump_locks(stdout); // FIXME, do this someplace better
            wasPressed = true;

            powerFSM.trigger(EVENT_PRESS);
        }
    } else if (wasPressed) {
        // we just did a release
        wasPressed = false;
    }
#endif

    // Show boot screen for first 3 seconds, then switch to normal operation.
    static bool showingBootScreen = true;
    if (showingBootScreen && (millis() > 3000)) {
        screen.stopBootScreen();
        showingBootScreen = false;
    }

    // Update the screen last, after we've figured out what to show.
    screen.debug()->setNodeNumbersStatus(nodeDB.getNumOnlineNodes(), nodeDB.getNumNodes());
    screen.debug()->setChannelNameStatus(channelSettings.name);
    screen.debug()->setPowerStatus(powerStatus);
    // TODO(#4): use something based on hdop to show GPS "signal" strength.
    screen.debug()->setGPSStatus(gps.hasLock() ? "ok" : ":(");
    screen.loop();

    // No GPS lock yet, let the OS put the main CPU in low power mode for 100ms (or until another interrupt comes in)
    // i.e. don't just keep spinning in loop as fast as we can.
    // DEBUG_MSG("msecs %d\n", msecstosleep);

    // FIXME - until button press handling is done by interrupt (see polling above) we can't sleep very long at all or buttons
    // feel slow
    msecstosleep = 10;

    delay(msecstosleep);
}
