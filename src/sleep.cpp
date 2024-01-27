#include "sleep.h"
#include "GPS.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "configuration.h"
#include "error.h"
#include "main.h"
#include "target_specific.h"

#ifdef ARCH_ESP32
#include "esp32/pm.h"
#include "esp_pm.h"
#include "mesh/wifi/WiFiAPClient.h"
#include "rom/rtc.h"
#include <driver/rtc_io.h>
#include <driver/uart.h>

esp_sleep_source_t wakeCause; // the reason we booted this time
#endif

#ifndef INCLUDE_vTaskSuspend
#define INCLUDE_vTaskSuspend 0
#endif

/// Called to ask any observers if they want to veto sleep. Return 1 to veto or 0 to allow sleep to happen
Observable<void *> preflightSleep;

/// Called to tell observers we are now entering sleep and you should prepare.  Must return 0
/// notifySleep will be called for light or deep sleep, notifyDeepSleep is only called for deep sleep
/// notifyGPSSleep will be called when config.position.gps_enabled is set to 0 or from buttonthread when GPS_POWER_TOGGLE is
/// enabled.
Observable<void *> notifySleep, notifyDeepSleep;
Observable<void *> notifyGPSSleep;

// deep sleep support
RTC_DATA_ATTR int bootCount = 0;

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
#ifdef ARCH_ESP32

    if (isWifiAvailable()) {
        /*
         *
         * There's a newly introduced bug in the espressif framework where WiFi is
         *   unstable when the frequency is less than 240mhz.
         *
         *   This mostly impacts WiFi AP mode but we'll bump the frequency for
         *     all WiFi use cases.
         * (Added: Dec 23, 2021 by Jm Casler)
         */
#ifndef CONFIG_IDF_TARGET_ESP32C3
        LOG_DEBUG("Setting CPU to 240mhz because WiFi is in use.\n");
        setCpuFrequencyMhz(240);
#endif
        return;
    }

// The Heltec LORA32 V1 runs at 26 MHz base frequency and doesn't react well to switching to 80 MHz...
#if !defined(ARDUINO_HELTEC_WIFI_LORA_32) && !defined(CONFIG_IDF_TARGET_ESP32C3)
    setCpuFrequencyMhz(on ? 240 : 80);
#endif

#endif
}

void setLed(bool ledOn)
{
#ifdef LED_PIN
    // toggle the led so we can get some rough sense of how often loop is pausing
    digitalWrite(LED_PIN, ledOn ^ LED_INVERTED);
#endif

#ifdef HAS_PMU
    if (pmu_found && PMU) {
        // blink the axp led
        PMU->setChargingLedMode(ledOn ? XPOWERS_CHG_LED_ON : XPOWERS_CHG_LED_OFF);
    }
#endif
}

// Perform power on init that we do on each wake from deep sleep
void initDeepSleep()
{
#ifdef ARCH_ESP32
    bootCount++;
    const char *reason;
    wakeCause = esp_sleep_get_wakeup_cause();

    switch (wakeCause) {
    case ESP_SLEEP_WAKEUP_EXT0:
        reason = "ext0 RTC_IO";
        break;
    case ESP_SLEEP_WAKEUP_EXT1:
        reason = "ext1 RTC_CNTL";
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        reason = "timer";
        break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        reason = "touchpad";
        break;
    case ESP_SLEEP_WAKEUP_ULP:
        reason = "ULP program";
        break;
    default:
        reason = "reset";
        break;
    }
    /*
      Not using yet because we are using wake on all buttons being low

      wakeButtons = esp_sleep_get_ext1_wakeup_status();       // If one of these buttons is set it was the reason we woke
      if (wakeCause == ESP_SLEEP_WAKEUP_EXT1 && !wakeButtons) // we must have been using the 'all buttons rule for waking' to
      support busted boards, assume button one was pressed wakeButtons = ((uint64_t)1) << buttons.gpios[0];
      */

#ifdef DEBUG_PORT
    // If we booted because our timer ran out or the user pressed reset, send those as fake events
    RESET_REASON hwReason = rtc_get_reset_reason(0);

    if (hwReason == RTCWDT_BROWN_OUT_RESET)
        reason = "brownout";

    if (hwReason == TG0WDT_SYS_RESET)
        reason = "taskWatchdog";

    if (hwReason == TG1WDT_SYS_RESET)
        reason = "intWatchdog";

    LOG_INFO("Booted, wake cause %d (boot count %d), reset_reason=%s\n", wakeCause, bootCount, reason);
#endif
#endif
}

bool doPreflightSleep()
{
    if (preflightSleep.notifyObservers(NULL) != 0)
        return false; // vetoed
    else
        return true;
}

/// Tell devices we are going to sleep and wait for them to handle things
static void waitEnterSleep(bool skipPreflight = false)
{
    if (!skipPreflight) {
        uint32_t now = millis();
        while (!doPreflightSleep()) {
            delay(100); // Kinda yucky - wait until radio says say we can shutdown (finished in process sends/receives)

            if (millis() - now > 30 * 1000) { // If we wait too long just report an error and go to sleep
                RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_SLEEP_ENTER_WAIT);
                assert(0); // FIXME - for now we just restart, need to fix bug #167
                break;
            }
        }
    }

    // Code that still needs to be moved into notifyObservers
    console->flush();          // send all our characters before we stop cpu clock
    setBluetoothEnable(false); // has to be off before calling light sleep

    notifySleep.notifyObservers(NULL);
}

void doDeepSleep(uint32_t msecToWake, bool skipPreflight = false)
{
    if (INCLUDE_vTaskSuspend && (msecToWake == portMAX_DELAY)) {
        LOG_INFO("Entering deep sleep forever\n");
    } else {
        LOG_INFO("Entering deep sleep for %u seconds\n", msecToWake / 1000);
    }

    // not using wifi yet, but once we are this is needed to shutoff the radio hw
    // esp_wifi_stop();
    waitEnterSleep(skipPreflight);
    notifyDeepSleep.notifyObservers(NULL);

    screen->doDeepSleep(); // datasheet says this will draw only 10ua

    nodeDB.saveToDisk();

    // Kill GPS power completely (even if previously we just had it in sleep mode)
    if (gps)
        gps->setGPSPower(false, false, 0);

    setLed(false);

#ifdef RESET_OLED
    digitalWrite(RESET_OLED, 1); // put the display in reset before killing its power
#endif

#if defined(VEXT_ENABLE_V03)
    if (heltec_version == 3) {
        digitalWrite(VEXT_ENABLE_V03, 1); // turn off the display power
    } else {
        digitalWrite(VEXT_ENABLE_V05, 0); // turn off the display power
    }
#elif defined(VEXT_ENABLE)
    digitalWrite(VEXT_ENABLE, 1); // turn off the display power
#endif

#ifdef HAS_PMU
    if (pmu_found && PMU) {
        // Obsolete comment: from back when we we used to receive lora packets while CPU was in deep sleep.
        // We no longer do that, because our light-sleep current draws are low enough and it provides fast start/low cost
        // wake.  We currently use deep sleep only for 'we want our device to actually be off - because our battery is
        // critically low'.  So in deep sleep we DO shut down power to LORA (and when we boot later we completely reinit it)
        //
        // No need to turn this off if the power draw in sleep mode really is just 0.2uA and turning it off would
        // leave floating input for the IRQ line
        // If we want to leave the radio receiving in would be 11.5mA current draw, but most of the time it is just waiting
        // in its sequencer (true?) so the average power draw should be much lower even if we were listinging for packets
        // all the time.

        uint8_t model = PMU->getChipModel();
        if (model == XPOWERS_AXP2101) {
            if (HW_VENDOR == meshtastic_HardwareModel_TBEAM) {
                // t-beam v1.2 radio power channel
                PMU->disablePowerOutput(XPOWERS_ALDO2); // lora radio power channel
            } else if (HW_VENDOR == meshtastic_HardwareModel_LILYGO_TBEAM_S3_CORE ||
                       HW_VENDOR == meshtastic_HardwareModel_T_WATCH_S3) {
                PMU->disablePowerOutput(XPOWERS_ALDO3); // lora radio power channel
            }
        } else if (model == XPOWERS_AXP192) {
            // t-beam v1.1 radio power channel
            PMU->disablePowerOutput(XPOWERS_LDO2); // lora radio power channel
        }
    }
#endif

    cpuDeepSleep(msecToWake);
}

#ifdef ARCH_ESP32
/**
 * enter light sleep (preserves ram but stops everything about CPU).
 *
 * Returns (after restoring hw state) when the user presses a button or we get a LoRa interrupt
 */
esp_sleep_wakeup_cause_t doLightSleep(uint64_t sleepMsec) // FIXME, use a more reasonable default
{
    // LOG_DEBUG("Enter light sleep\n");

    waitEnterSleep(false);

    uint64_t sleepUsec = sleepMsec * 1000LL;

    // NOTE! ESP docs say we must disable bluetooth and wifi before light sleep

    // We want RTC peripherals to stay on
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

#if defined(BUTTON_PIN) && defined(BUTTON_NEED_PULLUP)
    gpio_pullup_en((gpio_num_t)BUTTON_PIN);
#endif

#ifdef SERIAL0_RX_GPIO
    // We treat the serial port as a GPIO for a fast/low power way of waking, if we see a rising edge that means
    // someone started to send something

    // gpio 3 is RXD for serialport 0 on ESP32
    // Send a few Z characters to wake the port

    // this doesn't work on TBEAMs when the USB is depowered (causes bogus interrupts)
    // So we disable this "wake on serial" feature - because now when a TBEAM (only) has power connected it
    // never tries to go to sleep if the user is using the API
    // gpio_wakeup_enable((gpio_num_t)SERIAL0_RX_GPIO, GPIO_INTR_LOW_LEVEL);

    // doesn't help - I think the USB-UART chip losing power is pulling the signal llow
    // gpio_pullup_en((gpio_num_t)SERIAL0_RX_GPIO);

    // alas - can only work if using the refclock, which is limited to about 9600 bps
    // assert(uart_set_wakeup_threshold(UART_NUM_0, 3) == ESP_OK);
    // assert(esp_sleep_enable_uart_wakeup(0) == ESP_OK);
#endif
#ifdef BUTTON_PIN
#if SOC_PM_SUPPORT_EXT_WAKEUP
    esp_sleep_enable_ext0_wakeup((gpio_num_t)(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN),
                                 LOW); // when user presses, this button goes low
#else
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable((gpio_num_t)BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
#endif
#endif
#if defined(LORA_DIO1) && (LORA_DIO1 != RADIOLIB_NC)
    gpio_wakeup_enable((gpio_num_t)LORA_DIO1, GPIO_INTR_HIGH_LEVEL); // SX126x/SX128x interrupt, active high
#endif
#ifdef RF95_IRQ
    gpio_wakeup_enable((gpio_num_t)RF95_IRQ, GPIO_INTR_HIGH_LEVEL); // RF95 interrupt, active high
#endif
#ifdef PMU_IRQ
    // wake due to PMU can happen repeatedly if there is no battery installed or the battery fills
    if (pmu_found)
        gpio_wakeup_enable((gpio_num_t)PMU_IRQ, GPIO_INTR_LOW_LEVEL); // pmu irq
#endif
    auto res = esp_sleep_enable_gpio_wakeup();
    if (res != ESP_OK) {
        LOG_DEBUG("esp_sleep_enable_gpio_wakeup result %d\n", res);
    }
    assert(res == ESP_OK);
    res = esp_sleep_enable_timer_wakeup(sleepUsec);
    if (res != ESP_OK) {
        LOG_DEBUG("esp_sleep_enable_timer_wakeup result %d\n", res);
    }
    assert(res == ESP_OK);
    res = esp_light_sleep_start();
    if (res != ESP_OK) {
        LOG_DEBUG("esp_light_sleep_start result %d\n", res);
    }
    assert(res == ESP_OK);

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
#ifdef BUTTON_PIN
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        LOG_INFO("Exit light sleep gpio: btn=%d\n",
                 !digitalRead(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN));
    }
#endif

    return cause;
}

// not legal on the stock android ESP build

/**
 * enable modem sleep mode as needed and available.  Should lower our CPU current draw to an average of about 20mA.
 *
 * per https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/system/power_management.html
 *
 * supposedly according to https://github.com/espressif/arduino-esp32/issues/475 this is already done in arduino
 */
void enableModemSleep()
{
    static esp_pm_config_esp32_t esp32_config; // filled with zeros because bss

#if CONFIG_IDF_TARGET_ESP32S3
    esp32_config.max_freq_mhz = CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ;
#elif CONFIG_IDF_TARGET_ESP32S2
    esp32_config.max_freq_mhz = CONFIG_ESP32S2_DEFAULT_CPU_FREQ_MHZ;
#elif CONFIG_IDF_TARGET_ESP32C3
    esp32_config.max_freq_mhz = CONFIG_ESP32C3_DEFAULT_CPU_FREQ_MHZ;
#else
    esp32_config.max_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ;
#endif
    esp32_config.min_freq_mhz = 20; // 10Mhz is minimum recommended
    esp32_config.light_sleep_enable = false;
    int rv = esp_pm_configure(&esp32_config);
    LOG_DEBUG("Sleep request result %x\n", rv);
}
#endif