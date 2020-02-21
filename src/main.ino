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
#include "NodeDB.h"
#include "Periodic.h"
#include "esp32/pm.h"
#include "esp_pm.h"
#include "MeshRadio.h"

#ifdef T_BEAM_V10
#include "axp20x.h"
AXP20X_Class axp;
bool pmu_irq = false;
#endif
bool isCharging = false;
bool isUSBPowered = false;

bool ssd1306_found = false;
bool axp192_found = false;

bool packetSent, packetQueued;

// deep sleep support
RTC_DATA_ATTR int bootCount = 0;
esp_sleep_source_t wakeCause; // the reason we booted this time

#define xstr(s) str(s)
#define str(s) #s

// -----------------------------------------------------------------------------
// Application
// -----------------------------------------------------------------------------

/**
 * Control CPU core speed (80MHz vs 240MHz)
 * 
 * We leave CPU at full speed during init, but once loop is called switch to low speed (for a 50% power savings)
 * 
 */
void setCPUFast(bool on)
{
  setCpuFrequencyMhz(on ? 240 : 80);
}

static void setLed(bool ledOn)
{
#ifdef LED_PIN
  // toggle the led so we can get some rough sense of how often loop is pausing
  digitalWrite(LED_PIN, ledOn);
#endif

#ifdef T_BEAM_V10
  if (axp192_found)
  {
    // blink the axp led
    axp.setChgLEDMode(ledOn ? AXP20X_LED_LOW_LEVEL : AXP20X_LED_OFF);
  }
#endif
}

void setGPSPower(bool on)
{
#ifdef T_BEAM_V10
  if (axp192_found)
    axp.setPowerOutPut(AXP192_LDO3, on ? AXP202_ON : AXP202_OFF); // GPS main power
#endif
}

void doDeepSleep(uint64_t msecToWake)
{
  DEBUG_MSG("Entering deep sleep for %llu seconds\n", msecToWake / 1000);

  // not using wifi yet, but once we are this is needed to shutoff the radio hw
  // esp_wifi_stop();

  BLEDevice::deinit(false); // We are required to shutdown bluetooth before deep or light sleep

  screen_off(); // datasheet says this will draw only 10ua

  // Put radio in sleep mode (will still draw power but only 0.2uA)
  service.radio.rf95.sleep();

  nodeDB.saveToDisk();

#ifdef RESET_OLED
  digitalWrite(RESET_OLED, 1); // put the display in reset before killing its power
#endif

#ifdef VEXT_ENABLE
  digitalWrite(VEXT_ENABLE, 1); // turn off the display power
#endif

  setLed(false);

#ifdef T_BEAM_V10
  if (axp192_found)
  {
    // No need to turn this off if the power draw in sleep mode really is just 0.2uA and turning it off would
    // leave floating input for the IRQ line

    // If we want to leave the radio receving in would be 11.5mA current draw, but most of the time it is just waiting
    // in its sequencer (true?) so the average power draw should be much lower even if we were listinging for packets
    // all the time.

    // axp.setPowerOutPut(AXP192_LDO2, AXP202_OFF); // LORA radio

    setGPSPower(false);
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

  // We want RTC peripherals to stay on
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

#include "esp_bt_main.h"

bool bluetoothOn = true; // we turn it on during setup() so default on

void setBluetoothEnable(bool on)
{
  if (on != bluetoothOn)
  {
    DEBUG_MSG("Setting bluetooth enable=%d\n", on);

    bluetoothOn = on;
    if (on)
    {
      if (esp_bt_controller_enable(ESP_BT_MODE_BTDM) != ESP_OK)
        DEBUG_MSG("error reenabling bt controller\n");
      if (esp_bluedroid_enable() != ESP_OK)
        DEBUG_MSG("error reenabling bluedroid\n");
    }
    else
    {
      if (esp_bluedroid_disable() != ESP_OK)
        DEBUG_MSG("error disabling bluedroid\n");
      if (esp_bt_controller_disable() != ESP_OK)
        DEBUG_MSG("error disabling bt controller\n");
    }
  }
}

/**
 * enter light sleep (preserves ram but stops everything about CPU).
 * 
 * Returns (after restoring hw state) when the user presses a button or we get a LoRa interrupt
 */
void doLightSleep(uint32_t sleepMsec = 20 * 1000) // FIXME, use a more reasonable default
{
  DEBUG_MSG("Enter light sleep\n");
  uint64_t sleepUsec = sleepMsec * 1000LL;

  gps.prepareSleep(); // abandon in-process parsing
  setLed(false);      // Never leave led on while in light sleep

  // NOTE! ESP docs say we must disable bluetooth and wifi before light sleep

  // We want RTC peripherals to stay on
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  gpio_wakeup_enable((gpio_num_t)BUTTON_PIN, GPIO_INTR_LOW_LEVEL); // when user presses, this button goes low
  gpio_wakeup_enable((gpio_num_t)DIO0_GPIO, GPIO_INTR_HIGH_LEVEL); // RF95 interrupt, active high
#ifdef PMU_IRQ
  gpio_wakeup_enable((gpio_num_t)PMU_IRQ, GPIO_INTR_HIGH_LEVEL); // pmu irq
#endif
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(sleepUsec);
  esp_light_sleep_start();
  DEBUG_MSG("Exit light sleep\n");
}

/**
 * enable modem sleep mode as needed and available.  Should lower our CPU current draw to an average of about 20mA.
 * 
 * per https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/system/power_management.html
 * 
 * supposedly according to https://github.com/espressif/arduino-esp32/issues/475 this is already done in arduino
 */
void enableModemSleep()
{
  static esp_pm_config_esp32_t config; // filled with zeros because bss

  config.max_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ;
  config.min_freq_mhz = 10; // 10Mhz is minimum recommended
  config.light_sleep_enable = false;
  DEBUG_MSG("Sleep request result %x\n", esp_pm_configure(&config));
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
      pinMode(PMU_IRQ, INPUT_PULLUP);
      attachInterrupt(PMU_IRQ, [] {
        pmu_irq = true;
      },
                      RISING);

      axp.adc1Enable(AXP202_BATT_CUR_ADC1, 1);
      axp.enableIRQ(AXP202_VBUS_REMOVED_IRQ | AXP202_VBUS_CONNECT_IRQ | AXP202_BATT_REMOVED_IRQ | AXP202_BATT_CONNECT_IRQ, 1);
      axp.clearIRQ();
#endif

      isCharging = axp.isChargeing();
      isUSBPowered = axp.isVBUSPlug();
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
  DEBUG_MSG("%s %s\n", xstr(APP_NAME), str(APP_VERSION));

  // Don't init display if we don't have one or we are waking headless due to a timer event
  if (wakeCause == ESP_SLEEP_WAKEUP_TIMER)
    ssd1306_found = false; // forget we even have the hardware

  if (ssd1306_found)
    screen.setup();

  // Init GPS
  gps.setup();

  screen_print("Started...\n");

  service.init();

  bool useBluetooth = true;
  if (useBluetooth)
  {
    DEBUG_MSG("Starting bluetooth\n");
    BLEServer *serve = initBLE(getDeviceName(), HW_VENDOR, str(APP_VERSION)); // FIXME, use a real name based on the macaddr
    createMeshBluetoothService(serve);

    // Start advertising - this must be done _after_ creating all services
    serve->getAdvertising()->start();
  }

  setBluetoothEnable(false);
  setCPUFast(false); // 80MHz is fine for our slow peripherals
}

uint32_t ledBlinker()
{
  static bool ledOn;
  ledOn ^= 1;

  setLed(ledOn);

  // have a very sparse duty cycle of LED being on, unless charging, then blink 0.5Hz square wave rate to indicate that
  return isCharging ? 1000 : (ledOn ? 2 : 1000);
}

Periodic ledPeriodic(ledBlinker);

#if 0
// Turn off for now

uint32_t axpReads()
{
  axp.debugCharging();
  DEBUG_MSG("vbus current %f\n", axp.getVbusCurrent());
  DEBUG_MSG("charge current %f\n", axp.getBattChargeCurrent());
  DEBUG_MSG("bat voltage %f\n", axp.getBattVoltage());
  DEBUG_MSG("batt pct %d\n", axp.getBattPercentage());

  return 30 * 1000;
}

Periodic axpDebugOutput(axpReads);
#endif

void loop()
{
  uint32_t msecstosleep = 1000 * 30; // How long can we sleep before we again need to service the main loop?

  gps.loop();
  screen.loop();
  service.loop();

  if (nodeDB.updateGUI || nodeDB.updateTextMessage)
    screen.doWakeScreen();

  ledPeriodic.loop();
  // axpDebugOutput.loop();
  loopBLE();

#ifdef T_BEAM_V10
  if (axp192_found)
  {
#ifdef PMU_IRQ
    if (pmu_irq)
    {
      pmu_irq = false;
      axp.readIRQ();

      DEBUG_MSG("pmu irq!\n");

      isCharging = axp.isChargeing();
      isUSBPowered = axp.isVBUSPlug();

      axp.clearIRQ();
    }
#endif
  }
#endif

#ifdef BUTTON_PIN
  // if user presses button for more than 3 secs, discard our network prefs and reboot (FIXME, use a debounce lib instead of this boilerplate)
  static bool wasPressed = false;
  static uint32_t minPressMs; // what tick should we call this press long enough
  static uint32_t lastPingMs, lastPressMs;
  if (!digitalRead(BUTTON_PIN))
  {
    if (!wasPressed)
    { // just started a new press
      DEBUG_MSG("pressing\n");

      //doLightSleep();
      // esp_pm_dump_locks(stdout); // FIXME, do this someplace better
      wasPressed = true;

      uint32_t now = millis();
      lastPressMs = now;
      minPressMs = now + 3000;

      if (now - lastPingMs > 60 * 1000)
      { // if more than a minute since our last press, ask other nodes to update their state
        service.sendNetworkPing();
        lastPingMs = now;
      }

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
  // Don't deepsleep if we have USB power or if the user as pressed a button recently
  // !isUSBPowered <- doesn't work yet because the axp192 isn't letting the battery fully charge when we are awake - FIXME
  if (millis() - lastPressMs > MINWAKE_MSECS)
  {
    sleep();
  }
#endif

  // No GPS lock yet, let the OS put the main CPU in low power mode for 100ms (or until another interrupt comes in)
  // i.e. don't just keep spinning in loop as fast as we can.
  //DEBUG_MSG("msecs %d\n", msecstosleep);

  // FIXME - until button press handling is done by interrupt (see polling above) we can't sleep very long at all or buttons feel slow
  msecstosleep = 10;

  // while we have bluetooth on, we can't do light sleep, but once off stay in light_sleep all the time
  // we will wake from light sleep on button press or interrupt from the RF95 radio
  if (!bluetoothOn && !is_screen_on() && service.radio.rf95.canSleep() && gps.canSleep())
    doLightSleep(60 * 1000); // FIXME, wake up to briefly flash led, then go back to sleep (without repowering bluetooth)
  else
  {
    delay(msecstosleep);
  }
}