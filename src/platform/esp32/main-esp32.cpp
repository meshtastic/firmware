#include "PowerFSM.h"
#include "PowerMon.h"
#include "configuration.h"
#include "esp_task_wdt.h"
#include "main.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S2) && !MESHTASTIC_EXCLUDE_BLUETOOTH
#include "BleOta.h"
#include "nimble/NimbleBluetooth.h"
#endif

#include <WiFiOTA.h>

#if HAS_WIFI
#include "mesh/wifi/WiFiAPClient.h"
#endif

#include "esp_mac.h"
#include "meshUtils.h"
#include "sleep.h"
#include "soc/rtc.h"
#include "target_specific.h"
#include <Preferences.h>
#include <driver/rtc_io.h>
#include <nvs.h>
#include <nvs_flash.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S2) && !MESHTASTIC_EXCLUDE_BLUETOOTH
void setBluetoothEnable(bool enable)
{
#ifdef USE_WS5500
    if ((config.bluetooth.enabled == true) && (config.network.wifi_enabled == false))
#elif HAS_WIFI
    if (!isWifiAvailable() && config.bluetooth.enabled == true)
#else
    if (config.bluetooth.enabled == true)
#endif
    {
        if (!nimbleBluetooth) {
            nimbleBluetooth = new NimbleBluetooth();
        }
        if (enable && !nimbleBluetooth->isActive()) {
            powerMon->setState(meshtastic_PowerMon_State_BT_On);
            nimbleBluetooth->setup();
        }
        // For ESP32, no way to recover from bluetooth shutdown without reboot
        // BLE advertising automatically stops when MCU enters light-sleep(?)
        // For deep-sleep, shutdown hardware with nimbleBluetooth->deinit(). Requires reboot to reverse
    }
}
#else
void setBluetoothEnable(bool enable) {}
void updateBatteryLevel(uint8_t level) {}
#endif

void getMacAddr(uint8_t *dmac)
{
#if defined(CONFIG_IDF_TARGET_ESP32C6) && defined(CONFIG_SOC_IEEE802154_SUPPORTED)
    assert(esp_base_mac_addr_get(dmac) == ESP_OK);
#else
    assert(esp_efuse_mac_get_default(dmac) == ESP_OK);
#endif
}

#ifdef HAS_32768HZ
#define CALIBRATE_ONE(cali_clk) calibrate_one(cali_clk, #cali_clk)

static uint32_t calibrate_one(rtc_cal_sel_t cal_clk, const char *name)
{
    const uint32_t cal_count = 1000;
    // const float factor = (1 << 19) * 1000.0f; unused var?
    uint32_t cali_val;
    for (int i = 0; i < 5; ++i) {
        cali_val = rtc_clk_cal(cal_clk, cal_count);
    }
    return cali_val;
}

void enableSlowCLK()
{
    rtc_clk_32k_enable(true);

    CALIBRATE_ONE(RTC_CAL_RTC_MUX);
    uint32_t cal_32k = CALIBRATE_ONE(RTC_CAL_32K_XTAL);

    if (cal_32k == 0) {
        LOG_DEBUG("32K XTAL OSC has not started up");
    } else {
        rtc_clk_slow_freq_set(RTC_SLOW_FREQ_32K_XTAL);
        LOG_DEBUG("Switch RTC Source to 32.768Khz succeeded, using 32K XTAL");
        CALIBRATE_ONE(RTC_CAL_RTC_MUX);
        CALIBRATE_ONE(RTC_CAL_32K_XTAL);
    }
    CALIBRATE_ONE(RTC_CAL_RTC_MUX);
    CALIBRATE_ONE(RTC_CAL_32K_XTAL);
    if (rtc_clk_slow_freq_get() != RTC_SLOW_FREQ_32K_XTAL) {
        LOG_WARN("Failed to switch 32K XTAL RTC source to 32.768Khz !!! ");
        return;
    }
}
#endif

void esp32Setup()
{
    /* We explicitly don't want to do call randomSeed,
    // as that triggers the esp32 core to use a less secure pseudorandom function.
    uint32_t seed = esp_random();
    LOG_DEBUG("Set random seed %u", seed);
    randomSeed(seed);
    */

#ifdef ADC_V
    pinMode(ADC_V, INPUT);
#endif

    LOG_DEBUG("Total heap: %d", ESP.getHeapSize());
    LOG_DEBUG("Free heap: %d", ESP.getFreeHeap());
    LOG_DEBUG("Total PSRAM: %d", ESP.getPsramSize());
    LOG_DEBUG("Free PSRAM: %d", ESP.getFreePsram());

    nvs_stats_t nvs_stats;
    auto res = nvs_get_stats(NULL, &nvs_stats);
    assert(res == ESP_OK);
    LOG_DEBUG("NVS: UsedEntries %d, FreeEntries %d, AllEntries %d, NameSpaces %d", nvs_stats.used_entries, nvs_stats.free_entries,
              nvs_stats.total_entries, nvs_stats.namespace_count);

    LOG_DEBUG("Setup Preferences in Flash Storage");

    // Create object to store our persistent data
    Preferences preferences;
    preferences.begin("meshtastic", false);

    uint32_t rebootCounter = preferences.getUInt("rebootCounter", 0);
    rebootCounter++;
    preferences.putUInt("rebootCounter", rebootCounter);
    // store firmware version and hwrevision for access from OTA firmware
    String fwrev = preferences.getString("firmwareVersion", "");
    if (fwrev.compareTo(optstr(APP_VERSION)) != 0)
        preferences.putString("firmwareVersion", optstr(APP_VERSION));
    uint8_t hwven = preferences.getUInt("hwVendor", 0);
    if (hwven != HW_VENDOR)
        preferences.putUInt("hwVendor", HW_VENDOR);
    preferences.end();
    LOG_DEBUG("Number of Device Reboots: %d", rebootCounter);
#if !MESHTASTIC_EXCLUDE_BLUETOOTH
    String BLEOTA = BleOta::getOtaAppVersion();
    if (BLEOTA.isEmpty()) {
        LOG_INFO("No BLE OTA firmware available");
    } else {
        LOG_INFO("BLE OTA firmware version %s", BLEOTA.c_str());
    }
#endif
#if !MESHTASTIC_EXCLUDE_WIFI
    String version = WiFiOTA::getVersion();
    if (version.isEmpty()) {
        LOG_INFO("No WiFi OTA firmware available");
    } else {
        LOG_INFO("WiFi OTA firmware version %s", version.c_str());
    }
    WiFiOTA::initialize();
#endif

    // enableModemSleep();

// Since we are turning on watchdogs rather late in the release schedule, we really don't want to catch any
// false positives.  The wait-to-sleep timeout for shutting down radios is 30 secs, so pick 45 for now.
// #define APP_WATCHDOG_SECS 45
#define APP_WATCHDOG_SECS 90

#ifdef CONFIG_IDF_TARGET_ESP32C6
    esp_task_wdt_config_t *wdt_config = (esp_task_wdt_config_t *)malloc(sizeof(esp_task_wdt_config_t));
    wdt_config->timeout_ms = APP_WATCHDOG_SECS * 1000;
    wdt_config->trigger_panic = true;
    res = esp_task_wdt_init(wdt_config);
    assert(res == ESP_OK);
#else
    res = esp_task_wdt_init(APP_WATCHDOG_SECS, true);
    assert(res == ESP_OK);
#endif
    res = esp_task_wdt_add(NULL);
    assert(res == ESP_OK);

#ifdef HAS_32768HZ
    enableSlowCLK();
#endif
}

/// loop code specific to ESP32 targets
void esp32Loop()
{
    esp_task_wdt_reset(); // service our app level watchdog

    // for debug printing
    // radio.radioIf.canSleep();
}

void cpuDeepSleep(uint32_t msecToWake)
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

    Note: we don't isolate pins that are used for the LORA, LED, i2c, or ST7735 Display for the Chatter2, spi or the wake
    button(s), maybe we should not include any other GPIOs...
    */
#if SOC_RTCIO_HOLD_SUPPORTED
    static const uint8_t rtcGpios[] = {
#ifndef HELTEC_VISION_MASTER_E213
        // For this variant, >20mA leaks through the display if pin 2 held
        // Todo: check if it's safe to remove this pin for all variants
        2,
#endif
#ifndef USE_JTAG
        13,
#endif
        34, 35, 37};

    for (int i = 0; i < sizeof(rtcGpios); i++)
        rtc_gpio_isolate((gpio_num_t)rtcGpios[i]);
#endif

        // FIXME, disable internal rtc pullups/pulldowns on the non isolated pins. for inputs that we aren't using
        // to detect wake and in normal operation the external part drives them hard.
#ifdef BUTTON_PIN
        // Only GPIOs which are have RTC functionality can be used in this bit map: 0,2,4,12-15,25-27,32-39.
#if SOC_RTCIO_HOLD_SUPPORTED && SOC_PM_SUPPORT_EXT_WAKEUP
    uint64_t gpioMask = (1ULL << (config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN));
#endif

#ifdef BUTTON_NEED_PULLUP
    gpio_pullup_en((gpio_num_t)BUTTON_PIN);
#endif

    // Not needed because both of the current boards have external pullups
    // FIXME change polarity in hw so we can wake on ANY_HIGH instead - that would allow us to use all three buttons (instead
    // of just the first) gpio_pullup_en((gpio_num_t)BUTTON_PIN);

#ifdef ESP32S3_WAKE_TYPE
    esp_sleep_enable_ext1_wakeup(gpioMask, ESP32S3_WAKE_TYPE);
#else
#if SOC_PM_SUPPORT_EXT_WAKEUP
#ifdef CONFIG_IDF_TARGET_ESP32
    // ESP_EXT1_WAKEUP_ALL_LOW has been deprecated since esp-idf v5.4 for any other target.
    esp_sleep_enable_ext1_wakeup(gpioMask, ESP_EXT1_WAKEUP_ALL_LOW);
#else
    esp_sleep_enable_ext1_wakeup(gpioMask, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
#endif

#endif // #end ESP32S3_WAKE_TYPE
#endif

    // We want RTC peripherals to stay on
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    esp_sleep_enable_timer_wakeup(msecToWake * 1000ULL); // call expects usecs
    esp_deep_sleep_start();                              // TBD mA sleep current (battery)
}
