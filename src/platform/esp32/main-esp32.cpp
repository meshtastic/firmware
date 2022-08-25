#include "PowerFSM.h"
#include "configuration.h"
#include "esp_task_wdt.h"
#include "main.h"

#include "nimble/NimbleBluetooth.h"
#include "mesh/http/WiFiAPClient.h"

#include "sleep.h"
#include "target_specific.h"
#include "utils.h"
#include <Preferences.h>
#include <driver/rtc_io.h>
#include <nvs.h>
#include <nvs_flash.h>

NimbleBluetooth *nimbleBluetooth;

void getMacAddr(uint8_t *dmac)
{
    assert(esp_efuse_mac_get_default(dmac) == ESP_OK);
}

void setBluetoothEnable(bool on) {
    
    if (!isWifiAvailable() && config.bluetooth.enabled == true) {
        if (!nimbleBluetooth) {
            nimbleBluetooth = new NimbleBluetooth();
        }
        if (on && !nimbleBluetooth->isActive()) {
            nimbleBluetooth->setup();
        } else if (!on) {
            nimbleBluetooth->shutdown();
        }
    }
}

void esp32Setup()
{
    uint32_t seed = esp_random();
    DEBUG_MSG("Setting random seed %u\n", seed);
    randomSeed(seed); // ESP docs say this is fairly random

    DEBUG_MSG("Total heap: %d\n", ESP.getHeapSize());
    DEBUG_MSG("Free heap: %d\n", ESP.getFreeHeap());
    DEBUG_MSG("Total PSRAM: %d\n", ESP.getPsramSize());
    DEBUG_MSG("Free PSRAM: %d\n", ESP.getFreePsram());

    nvs_stats_t nvs_stats;
    auto res = nvs_get_stats(NULL, &nvs_stats);
    assert(res == ESP_OK);
    DEBUG_MSG("NVS: UsedEntries %d, FreeEntries %d, AllEntries %d, NameSpaces %d\n", nvs_stats.used_entries, nvs_stats.free_entries,
              nvs_stats.total_entries, nvs_stats.namespace_count);

    DEBUG_MSG("Setup Preferences in Flash Storage\n");

    // Create object to store our persistant data
    Preferences preferences;
    preferences.begin("meshtastic", false);

    uint32_t rebootCounter = preferences.getUInt("rebootCounter", 0);
    rebootCounter++;
    preferences.putUInt("rebootCounter", rebootCounter);
    preferences.end();
    DEBUG_MSG("Number of Device Reboots: %d\n", rebootCounter);

    // enableModemSleep();

// Since we are turning on watchdogs rather late in the release schedule, we really don't want to catch any
// false positives.  The wait-to-sleep timeout for shutting down radios is 30 secs, so pick 45 for now.
// #define APP_WATCHDOG_SECS 45
#define APP_WATCHDOG_SECS 90

    res = esp_task_wdt_init(APP_WATCHDOG_SECS, true);
    assert(res == ESP_OK);

    res = esp_task_wdt_add(NULL);
    assert(res == ESP_OK);
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
#endif


/// loop code specific to ESP32 targets
void esp32Loop()
{
    esp_task_wdt_reset(); // service our app level watchdog

    // for debug printing
    // radio.radioIf.canSleep();
}

void cpuDeepSleep(uint64_t msecToWake)
{
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
                                       13,
    /* 14, */ /* 15, */
#endif
                                       /* 25, */ 26, /* 27, */
                                       32,           33, 34, 35,
                                       36,           37
                                       /* 38, 39 */};

    for (int i = 0; i < sizeof(rtcGpios); i++)
        rtc_gpio_isolate((gpio_num_t)rtcGpios[i]);

    // FIXME, disable internal rtc pullups/pulldowns on the non isolated pins. for inputs that we aren't using
    // to detect wake and in normal operation the external part drives them hard.

    // We want RTC peripherals to stay on
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

#ifdef BUTTON_PIN
    // Only GPIOs which are have RTC functionality can be used in this bit map: 0,2,4,12-15,25-27,32-39.
    uint64_t gpioMask = (1ULL << BUTTON_PIN);

#ifdef BUTTON_NEED_PULLUP
    gpio_pullup_en((gpio_num_t)BUTTON_PIN);
#endif

    // Not needed because both of the current boards have external pullups
    // FIXME change polarity in hw so we can wake on ANY_HIGH instead - that would allow us to use all three buttons (instead of
    // just the first) gpio_pullup_en((gpio_num_t)BUTTON_PIN);

    esp_sleep_enable_ext1_wakeup(gpioMask, ESP_EXT1_WAKEUP_ALL_LOW);
#endif

    esp_sleep_enable_timer_wakeup(msecToWake * 1000ULL); // call expects usecs
    esp_deep_sleep_start();                              // TBD mA sleep current (battery)
}