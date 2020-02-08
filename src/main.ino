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

#include "configuration.h"
#include "rom/rtc.h"
#include <driver/rtc_io.h>
#include <TinyGPS++.h>
#include <Wire.h>
#include "BluetoothUtil.h"
#include "MeshBluetoothService.h"
#include "MeshService.h"
#include "GPS.h"
#include "screen.h"

#ifdef T_BEAM_V10
#include "axp20x.h"
AXP20X_Class axp;
bool pmu_irq = false;
String baChStatus = "No charging";
#endif

bool ssd1306_found = false;
bool axp192_found = false;

bool packetSent, packetQueued;

// deep sleep support
RTC_DATA_ATTR int bootCount = 0;
esp_sleep_source_t wakeCause; // the reason we booted this time

// -----------------------------------------------------------------------------
// Application
// -----------------------------------------------------------------------------

void doDeepSleep(uint64_t msecToWake)
{
  DEBUG_MSG("Entering deep sleep for %llu seconds\n", msecToWake / 1000);

  // not using wifi yet, but once we are this is needed to shutoff the radio hw
  // esp_wifi_stop();

  BLEDevice::deinit(false); // We are required to shutdown bluetooth before deep or light sleep

  screen_off(); // datasheet says this will draw only 10ua

  // Put radio in sleep mode (will still draw power but only 0.2uA)
  service.radio.sleep();

#ifdef RESET_OLED
  digitalWrite(RESET_OLED, 1); // put the display in reset before killing its power
#endif

#ifdef VEXT_ENABLE
  digitalWrite(VEXT_ENABLE, 1); // turn off the display power
#endif

#ifdef LED_PIN
  digitalWrite(LED_PIN, 0); // turn off the led
#endif

#ifdef T_BEAM_V10
  if (axp192_found)
  {
    axp.setChgLEDMode(AXP20X_LED_OFF); // turn off the AXP LED

    // No need to turn this off if the power draw in sleep mode really is just 0.2uA and turning it off would
    // leave floating input for the IRQ line

    // If we want to leave the radio receving in would be 11.5mA current draw, but most of the time it is just waiting
    // in its sequencer (true?) so the average power draw should be much lower even if we were listinging for packets
    // all the time.

    // axp.setPowerOutPut(AXP192_LDO2, AXP202_OFF); // LORA radio

    axp.setPowerOutPut(AXP192_LDO3, AXP202_OFF); // GPS main power
  }
#endif

  /*
  Some ESP32 IOs have internal pullups or pulldowns, which are enabled by default. 
  If an external circuit drives this pin in deep sleep mode, current consumption may 
  increase due to current flowing through these pullups and pulldowns.

  To isolate a pin, preventing extra current draw, call rtc_gpio_isolate() function.
  For example, on ESP32-WROVER module, GPIO12 is pulled up externally. 
  GPIO12 also has an internal pulldown in the ESP32 chip. This means that in deep sleep, 
  some current will flow through these external and internal resistors, increasing deep 
  sleep current above the minimal possible value. 

  Note: we don't isolate pins that are used for the LORA, LED, i2c, spi or the wake button
  */
  static const uint8_t rtcGpios[] = {/* 0, */ 2,
  /* 4, */
#ifndef USE_JTAG
                                     12, 13, /* 14, */ /* 15, */
#endif
                                     /* 25, */ 26, /* 27, */
                                     32, 33, 34, 35, 36, 37, /* 38, */ 39};

  for (int i = 0; i < sizeof(rtcGpios); i++)
    rtc_gpio_isolate((gpio_num_t)rtcGpios[i]);

  // FIXME, disable internal rtc pullups/pulldowns on the non isolated pins. for inputs that we aren't using
  // to detect wake and in normal operation the external part drives them hard.

  // FIXME - use an external 10k pulldown so we can leave the RTC peripherals powered off
  // until then we need the following lines
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

#ifdef BUTTON_PIN
  // Only GPIOs which are have RTC functionality can be used in this bit map: 0,2,4,12-15,25-27,32-39.
  uint64_t gpioMask = (1ULL << BUTTON_PIN);

  // Not needed because both of the current boards have external pullups
  // FIXME change polarity in hw so we can wake on ANY_HIGH instead - that would allow us to use all three buttons (instead of just the first)
  // gpio_pullup_en((gpio_num_t)BUTTON_PIN);

  esp_sleep_enable_ext1_wakeup(gpioMask, ESP_EXT1_WAKEUP_ALL_LOW);
#endif

  esp_sleep_enable_timer_wakeup(msecToWake * 1000ULL); // call expects usecs
  esp_deep_sleep_start();                              // TBD mA sleep current (battery)
}

void sleep()
{
#ifdef SLEEP_MSECS

  // If the user has a screen, tell them we are about to sleep
  if (ssd1306_found)
  {
    // Show the going to sleep message on the screen
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "Sleeping in %3.1fs\n", (MESSAGE_TO_SLEEP_DELAY / 1000.0));
    screen_print(buffer);

    // Wait for MESSAGE_TO_SLEEP_DELAY millis to sleep
    delay(MESSAGE_TO_SLEEP_DELAY);
  }

  // We sleep for the interval between messages minus the current millis
  // this way we distribute the messages evenly every SEND_INTERVAL millis
  doDeepSleep(SLEEP_MSECS);

#endif
}

void scanI2Cdevice(void)
{
  byte err, addr;
  int nDevices = 0;
  for (addr = 1; addr < 127; addr++)
  {
    Wire.beginTransmission(addr);
    err = Wire.endTransmission();
    if (err == 0)
    {
      DEBUG_MSG("I2C device found at address 0x%x\n", addr);

      nDevices++;

      if (addr == SSD1306_ADDRESS)
      {
        ssd1306_found = true;
        DEBUG_MSG("ssd1306 display found\n");
      }
#ifdef T_BEAM_V10
      if (addr == AXP192_SLAVE_ADDRESS)
      {
        axp192_found = true;
        DEBUG_MSG("axp192 PMU found\n");
      }
#endif
    }
    else if (err == 4)
    {
      DEBUG_MSG("Unknow error at address 0x%x\n", addr);
    }
  }
  if (nDevices == 0)
    DEBUG_MSG("No I2C devices found\n");
  else
    DEBUG_MSG("done\n");
}

/**
 * Init the power manager chip
 * 
 * axp192 power 
    DCDC1 0.7-3.5V @ 1200mA max -> OLED // If you turn this off you'll lose comms to the axp192 because the OLED and the axp192 share the same i2c bus, instead use ssd1306 sleep mode
    DCDC2 -> unused
    DCDC3 0.7-3.5V @ 700mA max -> ESP32 (keep this on!)
    LDO1 30mA -> charges GPS backup battery // charges the tiny J13 battery by the GPS to power the GPS ram (for a couple of days), can not be turned off
    LDO2 200mA -> LORA
    LDO3 200mA -> GPS
 */
void axp192Init()
{
#ifdef T_BEAM_V10
  if (axp192_found)
  {
    if (!axp.begin(Wire, AXP192_SLAVE_ADDRESS))
    {
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

      axp.debugCharging();

      pinMode(PMU_IRQ, INPUT_PULLUP);
      attachInterrupt(PMU_IRQ, [] {
        pmu_irq = true;
      },
                      FALLING);

      axp.adc1Enable(AXP202_BATT_CUR_ADC1, 1);
      axp.enableIRQ(AXP202_VBUS_REMOVED_IRQ | AXP202_VBUS_CONNECT_IRQ | AXP202_BATT_REMOVED_IRQ | AXP202_BATT_CONNECT_IRQ, 1);
      axp.clearIRQ();

      if (axp.isChargeing())
      {
        baChStatus = "Charging";
      }
    }
    else
    {
      DEBUG_MSG("AXP192 Begin FAIL\n");
    }
  }
  else
  {
    DEBUG_MSG("AXP192 not found\n");
  }
#endif
}

// Perform power on init that we do on each wake from deep sleep
void initDeepSleep()
{
  bootCount++;
  wakeCause = esp_sleep_get_wakeup_cause();
  /* 
    Not using yet because we are using wake on all buttons being low

    wakeButtons = esp_sleep_get_ext1_wakeup_status();       // If one of these buttons is set it was the reason we woke
    if (wakeCause == ESP_SLEEP_WAKEUP_EXT1 && !wakeButtons) // we must have been using the 'all buttons rule for waking' to support busted boards, assume button one was pressed
        wakeButtons = ((uint64_t)1) << buttons.gpios[0];
    */

  DEBUG_MSG("booted, wake cause %d (boot count %d)\n", wakeCause, bootCount);
}

const char *getDeviceName()
{
  uint8_t dmac[6];
  assert(esp_efuse_mac_get_default(dmac) == ESP_OK);

  // Meshtastic_ab3c
  static char name[20];
  sprintf(name, "Meshtastic_%02x%02x", dmac[4], dmac[5]);
  return name;
}

void setup()
{
// Debug
#ifdef DEBUG_PORT
  DEBUG_PORT.begin(SERIAL_BAUD);
#endif

  initDeepSleep();
  // delay(1000); FIXME - remove

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

  axp192Init();

  // Buttons & LED
#ifdef BUTTON_PIN
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(BUTTON_PIN, 1);
#endif
#ifdef LED_PIN
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, 1); // turn on for now
#endif

  // Hello
  DEBUG_MSG(APP_NAME " " APP_VERSION "\n");

  // Don't init display if we don't have one or we are waking headless due to a timer event
  if (wakeCause == ESP_SLEEP_WAKEUP_TIMER)
    ssd1306_found = false; // forget we even have the hardware

  if (ssd1306_found)
    screen_setup();

  // Init GPS
  gps.setup();

  screen_print("Started...\n");

  service.init();

  bool useBluetooth = true;
  if (useBluetooth)
  {
    DEBUG_MSG("Starting bluetooth\n");
    BLEServer *serve = initBLE(getDeviceName(), HW_VENDOR, APP_VERSION); // FIXME, use a real name based on the macaddr
    createMeshBluetoothService(serve);
  }
}

void loop()
{
  uint32_t msecstosleep = 1000 * 30; // How long can we sleep before we again need to service the main loop?

  gps.loop();
  msecstosleep = min(screen_loop(), msecstosleep);
  service.loop();
  loopBLE();

  static bool ledon;
  ledon ^= 1;

#ifdef LED_PIN
  // toggle the led so we can get some rough sense of how often loop is pausing
  digitalWrite(LED_PIN, ledon);
#endif

#ifdef T_BEAM_V10
  if (axp192_found)
  {
    // blink the axp led
    axp.setChgLEDMode(ledon ? AXP20X_LED_LOW_LEVEL : AXP20X_LED_OFF);
  }
#endif

#ifdef BUTTON_PIN
  // if user presses button for more than 3 secs, discard our network prefs and reboot (FIXME, use a debounce lib instead of this boilerplate)
  static bool wasPressed = false;
  static uint32_t minPressMs; // what tick should we call this press long enough
  if (!digitalRead(BUTTON_PIN))
  {
    if (!wasPressed)
    { // just started a new press
      DEBUG_MSG("pressing\n");
      wasPressed = true;
      minPressMs = millis() + 3000;
      screen_press();
    }
  }
  else if (wasPressed)
  {
    // we just did a release
    wasPressed = false;
    if (millis() > minPressMs)
    {
      // held long enough
      screen_print("Erasing prefs");
      delay(5000); // Give some time to read the screen
      // ESP.restart();
    }
  }
#endif

#ifdef MINWAKE_MSECS
  if (millis() > MINWAKE_MSECS)
  {
    sleep();
  }
#endif

  // No GPS lock yet, let the OS put the main CPU in low power mode for 100ms (or until another interrupt comes in)
  // i.e. don't just keep spinning in loop as fast as we can.
  //DEBUG_MSG("msecs %d\n", msecstosleep);

  // FIXME - until button press handling is done by interrupt (see polling above) we can't sleep very long at all or buttons feel slow
  msecstosleep = 10;
  delay(msecstosleep);
}