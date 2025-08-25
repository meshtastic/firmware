#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif

#include "Default.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerMon.h"
#include "concurrency/Lock.h"
#include "detect/LoRaRadioType.h"
#include "error.h"
#include "main.h"
#include "modules/StatusLEDModule.h"
#include "sleep.h"
#include "target_specific.h"

#ifdef ARCH_ESP32
#ifdef HAS_ESP32_PM_SUPPORT
#include "esp32/pm.h"
#include "esp_pm.h"
#endif
#if HAS_WIFI
#include "mesh/wifi/WiFiAPClient.h"
#endif
#include "rom/rtc.h"
#include <driver/rtc_io.h>
#include <driver/uart.h>
#endif
#include "Throttle.h"

#ifdef USE_XL9555
#include "ExtensionIOXL9555.hpp"
extern ExtensionIOXL9555 io;
#endif

#ifdef HAS_PPM
#include <XPowersLib.h>
extern XPowersPPM *PPM;
#endif

#ifndef INCLUDE_vTaskSuspend
#define INCLUDE_vTaskSuspend 0
#endif

/// Called to ask any observers if they want to veto sleep. Return 1 to veto or 0 to allow sleep to happen
Observable<void *> preflightSleep;

/// Called to tell observers we are now entering (deep) sleep and you should prepare.  Must return 0
Observable<void *> notifyDeepSleep;

/// Called to tell observers we are rebooting ASAP.  Must return 0
Observable<void *> notifyReboot;

#ifdef ARCH_ESP32
// Wake cause when returning from sleep
esp_sleep_source_t wakeCause;

/// Called to tell observers that light sleep is about to begin
Observable<void *> notifyLightSleep;

/// Called to tell observers that light sleep has just ended, and why it ended
Observable<esp_sleep_wakeup_cause_t> notifyLightSleepEnd;

#ifdef HAS_ESP32_PM_SUPPORT
esp_pm_lock_handle_t pmLightSleepLock;
#endif

// this are imported symbol with target-specific implementation
bool setSerialClockToRefTick(int uart_num);

// restores GPIO function after sleep
void gpioReset(void);
// enables button wake-up interrupt
void enableButtonInterrupt(void);
// enables LoRa wake-up-interrupt
void enableLoraInterrupt(void);

bool shouldLoraWake(uint32_t msecToWake);
#endif

// this are imported symbol with target-specific implementation
void cpuDeepSleep(uint32_t msecToWake);

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
    uint8_t targetFrequency = on ? 240 : 80;
    setCpuFrequencyMhz(targetFrequency);
    LOG_INFO("CPU frequency set to %u MHz", targetFrequency);
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

#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    // If we booted because our timer ran out or the user pressed reset, send those as fake events
    RESET_REASON hwReason = rtc_get_reset_reason(0);

    if (hwReason == RTCWDT_BROWN_OUT_RESET)
        reason = "brownout";

    if (hwReason == TG0WDT_SYS_RESET)
        reason = "taskWatchdog";

    if (hwReason == TG1WDT_SYS_RESET)
        reason = "intWatchdog";

    LOG_INFO("Booted, wake cause %d (boot count %d), reset_reason=%s", wakeCause, bootCount, reason);
#endif

#ifdef ARCH_ESP32
    if (wakeCause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        gpioReset();
    }
#endif
#endif
}

bool doPreflightSleep()
{
    return preflightSleep.notifyObservers(NULL) == 0;
}

/// Tell devices we are going to sleep and wait for them to handle things
static void waitEnterSleep(bool skipPreflight = false)
{
    if (!skipPreflight) {
        uint32_t now = millis();
        while (!doPreflightSleep()) {
            delay(100); // Kinda yucky - wait until radio says say we can shutdown (finished in process sends/receives)

            if (!Throttle::isWithinTimespanMs(now,
                                              THIRTY_SECONDS_MS)) { // If we wait too long just report an error and go to sleep
                RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_SLEEP_ENTER_WAIT);
                assert(0); // FIXME - for now we just restart, need to fix bug #167
                break;
            }
        }
    }

    // Code that still needs to be moved into notifyObservers
    console->flush();          // send all our characters before we stop cpu clock
    setBluetoothEnable(false); // has to be off before calling light sleep
}

void doDeepSleep(uint32_t msecToWake, bool skipPreflight = false, bool skipSaveNodeDb = false)
{
    if (INCLUDE_vTaskSuspend && (msecToWake == portMAX_DELAY)) {
        LOG_INFO("Enter deep sleep forever");

    } else {
        LOG_INFO("Enter deep sleep for %u seconds", msecToWake / 1000);
    }

    // not using wifi yet, but once we are this is needed to shutoff the radio hw
    // esp_wifi_stop();
    waitEnterSleep(skipPreflight);

#if defined(ARCH_ESP32) && !MESHTASTIC_EXCLUDE_BLUETOOTH
    // Full shutdown of bluetooth hardware
    if (nimbleBluetooth)
        nimbleBluetooth->deinit();
#endif

#ifdef ARCH_ESP32
    if (!shouldLoraWake(msecToWake))
        notifyDeepSleep.notifyObservers(NULL);
#else
    notifyDeepSleep.notifyObservers(NULL);
#endif

    powerMon->setState(meshtastic_PowerMon_State_CPU_DeepSleep);
    if (screen)
        screen->doDeepSleep(); // datasheet says this will draw only 10ua

    if (!skipSaveNodeDb) {
        nodeDB->saveToDisk();
    }

#ifdef PIN_POWER_EN
    digitalWrite(PIN_POWER_EN, LOW);
    pinMode(PIN_POWER_EN, INPUT); // power off peripherals
#endif

#ifdef RAK_WISMESH_TAP_V2
    digitalWrite(SDCARD_CS, LOW);
#endif

#ifdef TRACKER_T1000_E
#ifdef GNSS_AIROHA
    digitalWrite(GPS_VRTC_EN, LOW);
    digitalWrite(PIN_GPS_RESET, LOW);
    digitalWrite(GPS_SLEEP_INT, LOW);
    digitalWrite(GPS_RTC_INT, LOW);
    pinMode(GPS_RESETB_OUT, OUTPUT);
    digitalWrite(GPS_RESETB_OUT, LOW);
#endif

#ifdef BUZZER_EN_PIN
    digitalWrite(BUZZER_EN_PIN, LOW);
#endif

#ifdef PIN_3V3_EN
    digitalWrite(PIN_3V3_EN, LOW);
#endif
#ifdef PIN_WD_EN
    digitalWrite(PIN_WD_EN, LOW);
#endif
#endif
    statusLEDModule->setPowerLED(false);
#ifdef RESET_OLED
    digitalWrite(RESET_OLED, 1); // put the display in reset before killing its power
#endif

#if defined(VEXT_ENABLE)
    digitalWrite(VEXT_ENABLE, !VEXT_ON_VALUE); // turn on the display power
#endif

#ifdef ARCH_ESP32
    if (shouldLoraWake(msecToWake)) {
        enableLoraInterrupt();
    }
    enableButtonInterrupt();
#endif

#ifdef HAS_PPM
    if (PPM) {
        LOG_INFO("PMM shutdown");
        console->flush();
        PPM->shutdown();
    }
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
        // in its sequencer (true?) so the average power draw should be much lower even if we were listening for packets
        // all the time.
        PMU->setChargingLedMode(XPOWERS_CHG_LED_OFF);

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
        if (msecToWake == portMAX_DELAY) {
            LOG_INFO("PMU shutdown");
            console->flush();
            PMU->shutdown();
        }
    }
#endif

#if !MESHTASTIC_EXCLUDE_I2C && defined(ARCH_ESP32) && defined(I2C_SDA)
    // Added by https://github.com/meshtastic/firmware/pull/4418
    // Possibly to support Heltec Capsule Sensor?
    Wire.end();
    pinMode(I2C_SDA, ANALOG);
    pinMode(I2C_SCL, ANALOG);
#endif

#if defined(ARCH_ESP32) && defined(I2C_SDA1)
    // Added by https://github.com/meshtastic/firmware/pull/4418
    // Possibly to support Heltec Capsule Sensor?
    Wire1.end();
    pinMode(I2C_SDA1, ANALOG);
    pinMode(I2C_SCL1, ANALOG);
#endif

    console->flush();
    cpuDeepSleep(msecToWake);
}

#ifdef ARCH_ESP32
#ifdef HAS_ESP32_DYNAMIC_LIGHT_SLEEP
static bool pmLightSleepLockAcquired;
#endif
static concurrency::Lock *lightSleepConcurrencyLock;

/**
 * enter light sleep (preserves ram but stops everything about CPU).
 *
 * Returns (after restoring hw state) when the user presses a button or we get a LoRa interrupt
 */
void doLightSleep(uint32_t sleepMsec)
{
    esp_err_t res;

    assert(lightSleepConcurrencyLock);
    lightSleepConcurrencyLock->lock();

#ifndef HAS_ESP32_DYNAMIC_LIGHT_SLEEP
    assert(sleepMsec != LIGHT_SLEEP_ABORT);
    assert(sleepMsec != LIGHT_SLEEP_DYNAMIC);
#else
    if (!pmLightSleepLockAcquired) {
        if (sleepMsec == LIGHT_SLEEP_DYNAMIC) {
            lightSleepConcurrencyLock->unlock();
            return;
        }

        res = esp_pm_lock_acquire(pmLightSleepLock);
        assert(res == ESP_OK);

        wakeCause = esp_sleep_get_wakeup_cause();

        pmLightSleepLockAcquired = true;

        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        gpioReset();

        notifyLightSleepEnd.notifyObservers(wakeCause);
    }

    if (sleepMsec == LIGHT_SLEEP_ABORT) {
        lightSleepConcurrencyLock->unlock();
        return;
    }
#endif

    enableLoraInterrupt();
    enableButtonInterrupt();

    if (sleepMsec != LIGHT_SLEEP_DYNAMIC) {
        res = esp_sleep_enable_timer_wakeup(sleepMsec * 1000LL);
        assert(res == ESP_OK);
    }

    res = uart_set_wakeup_threshold(UART_NUM_0, 3);
    assert(res == ESP_OK);

    res = esp_sleep_enable_uart_wakeup(UART_NUM_0);
    assert(res == ESP_OK);

#if defined(VEXT_ENABLE)
    gpio_hold_en((gpio_num_t)VEXT_ENABLE);
#endif
#if defined(RESET_OLED)
    gpio_hold_en((gpio_num_t)RESET_OLED);
#endif

#ifdef ROTARY_PRESS
    gpio_wakeup_enable((gpio_num_t)ROTARY_PRESS, GPIO_INTR_LOW_LEVEL);
    assert(res == ESP_OK);
#endif
#ifdef KB_INT
    gpio_wakeup_enable((gpio_num_t)KB_INT, GPIO_INTR_LOW_LEVEL);
    assert(res == ESP_OK);
#endif
#if defined(INPUTDRIVER_ENCODER_BTN)
    res = gpio_wakeup_enable((gpio_num_t)INPUTDRIVER_ENCODER_BTN, GPIO_INTR_LOW_LEVEL);
    assert(res == ESP_OK);
#endif
#ifdef WAKE_ON_TOUCH
    res = gpio_wakeup_enable((gpio_num_t)SCREEN_TOUCH_INT, GPIO_INTR_LOW_LEVEL);
    assert(res == ESP_OK);
#endif
#ifdef PMU_IRQ
    // wake due to PMU can happen repeatedly if there is no battery installed or the battery fills
    if (pmu_found) {
        res = gpio_wakeup_enable((gpio_num_t)PMU_IRQ, GPIO_INTR_LOW_LEVEL); // pmu irq
        assert(res == ESP_OK);
    }
#endif

    res = esp_sleep_enable_gpio_wakeup();
    assert(res == ESP_OK);

    notifyLightSleep.notifyObservers(NULL);

    console->flush();

    if (sleepMsec != LIGHT_SLEEP_DYNAMIC) {
        esp_light_sleep_start();

        wakeCause = esp_sleep_get_wakeup_cause();

        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        gpioReset();

        notifyLightSleepEnd.notifyObservers(wakeCause);

    } else {
#ifdef HAS_ESP32_DYNAMIC_LIGHT_SLEEP
        res = esp_pm_lock_release(pmLightSleepLock);
        assert(res == ESP_OK);
        pmLightSleepLockAcquired = false;
#endif
    }

    lightSleepConcurrencyLock->unlock();
}

// Initialize power management settings to allow light sleep
void initLightSleep()
{
    esp_err_t res;
    bool dfsSupported = true;

#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2)
    if (dfsSupported) {
        dfsSupported &= setSerialClockToRefTick(UART_NUM_0);
    }
#endif

#ifdef HAS_ESP32_PM_SUPPORT
    res = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "meshtastic", &pmLightSleepLock);
    assert(res == ESP_OK);

    res = esp_pm_lock_acquire(pmLightSleepLock);
    assert(res == ESP_OK);

    esp_pm_config_esp32_t pm_config;
    pm_config.max_freq_mhz = 80;
    pm_config.min_freq_mhz = dfsSupported ? 20 : pm_config.max_freq_mhz;
#ifdef HAS_ESP32_DYNAMIC_LIGHT_SLEEP
    pm_config.light_sleep_enable = true;
#else
    pm_config.light_sleep_enable = false;
#endif

    res = esp_pm_configure(&pm_config);
    assert(res == ESP_OK);

    LOG_INFO("PM config enabled - min_freq_mhz=%d, max_freq_mhz=%d, light_sleep_enable=%d", pm_config.min_freq_mhz,
             pm_config.max_freq_mhz, pm_config.light_sleep_enable);
#endif

    lightSleepConcurrencyLock = new concurrency::Lock();

#ifdef HAS_ESP32_DYNAMIC_LIGHT_SLEEP
    pmLightSleepLockAcquired = true;
#endif
}

void gpioReset()
{
    esp_err_t res;

    // deinitialize RTC GPIOs and holds
    for (uint8_t i = 0; i <= GPIO_NUM_MAX; i++) {
#ifdef SOC_PM_SUPPORT_EXT_WAKEUP
        if (rtc_gpio_is_valid_gpio((gpio_num_t)i)) {
            rtc_gpio_hold_dis((gpio_num_t)i);
            rtc_gpio_deinit((gpio_num_t)i);
        }
#endif
        if (GPIO_IS_VALID_OUTPUT_GPIO((gpio_num_t)i)) {
            gpio_hold_dis((gpio_num_t)i);
        }
    }

    // restore negative-edge interrupt triggers for input pins
#ifdef INPUTDRIVER_ENCODER_BTN
    res = gpio_set_intr_type((gpio_num_t)INPUTDRIVER_ENCODER_BTN, GPIO_INTR_NEGEDGE);
    assert(res == ESP_OK);
#endif
#ifdef WAKE_ON_TOUCH
    res = gpio_set_intr_type((gpio_num_t)SCREEN_TOUCH_INT, GPIO_INTR_NEGEDGE);
    assert(res == ESP_OK);
#endif
#ifdef PMU_IRQ
    if (pmu_found) {
        res = gpio_set_intr_type((gpio_num_t)PMU_IRQ, GPIO_INTR_NEGEDGE);
        assert(res == ESP_OK);
    }
#endif

    gpio_num_t pin = GPIO_NUM_NC;

#if defined(LORA_DIO1) && (LORA_DIO1 != GPIO_NUM_NC)
    pin = (gpio_num_t)LORA_DIO1;
#elif defined(RF95_IRQ) && (RF95_IRQ != GPIO_NUM_NC)
    pin = (gpio_num_t)RF95_IRQ;
#endif

    // need to restore original GPIO interrupt trigger when it's not RTC GPIO or we don't support EXT wakeup
    if (pin != GPIO_NUM_NC) {
#ifdef SOC_PM_SUPPORT_EXT_WAKEUP
        if (!rtc_gpio_is_valid_gpio(pin)) {
#endif
            res = gpio_set_intr_type(pin, GPIO_INTR_POSEDGE);
            assert(res == ESP_OK);
#ifdef SOC_PM_SUPPORT_EXT_WAKEUP
        }
#endif
    }

    pin = GPIO_NUM_NC;

    if (config.device.button_gpio) {
        pin = (gpio_num_t)config.device.button_gpio;
    }

#ifdef BUTTON_PIN
    if (pin == GPIO_NUM_NC) {
        pin = (gpio_num_t)BUTTON_PIN;
    }
#endif

    // need to restore original GPIO interrupt trigger when it's not RTC GPIO or we don't support EXT wakeup
    if (pin != GPIO_NUM_NC) {
#ifdef SOC_PM_SUPPORT_EXT_WAKEUP
        if (!rtc_gpio_is_valid_gpio(pin)) {
#endif
            res = gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
            assert(res == ESP_OK);
#ifdef SOC_PM_SUPPORT_EXT_WAKEUP
        }
#endif
    }
}

void enableButtonInterrupt()
{
    esp_err_t res;
    gpio_num_t pin;

    pin = GPIO_NUM_NC;

    if (config.device.button_gpio) {
        pin = (gpio_num_t)config.device.button_gpio;
    }

#ifdef BUTTON_PIN
    if (pin == GPIO_NUM_NC) {
        pin = (gpio_num_t)BUTTON_PIN;
    }
#endif

    if (pin == GPIO_NUM_NC) {
        return;
    }

#ifdef SOC_PM_SUPPORT_EXT_WAKEUP
    if (rtc_gpio_is_valid_gpio(pin)) {
        LOG_DEBUG("Setup button pin (GPIO%02d) with wakeup by ext1 source", pin);
#ifdef BUTTON_NEED_PULLUP
        res = rtc_gpio_pullup_en(pin);
        assert(res == ESP_OK);
#endif
        res = rtc_gpio_hold_en((gpio_num_t)pin);
        assert(res == ESP_OK);
#ifdef CONFIG_IDF_TARGET_ESP32
        res = esp_sleep_enable_ext1_wakeup(1ULL << pin, ESP_EXT1_WAKEUP_ALL_LOW);
#else
        res = esp_sleep_enable_ext1_wakeup(1ULL << pin, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
        assert(res == ESP_OK);

    } else {
        LOG_DEBUG("Setup button pin (GPIO%02d) with wakeup by GPIO interrupt", pin);
#ifdef BUTTON_NEED_PULLUP
        res = gpio_pullup_en(pin);
        assert(res == ESP_OK);
#endif
        res = gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
        assert(res == ESP_OK);
        if (GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
            res = gpio_hold_en((gpio_num_t)pin);
            assert(res == ESP_OK);
        }
    }
#else
#ifdef BUTTON_NEED_PULLUP
    res = gpio_pullup_en(pin);
    assert(res == ESP_OK);
#endif
    res = gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
    assert(res == ESP_OK);
    LOG_DEBUG("Setup button pin (GPIO%02d) with wakeup by GPIO interrupt", pin);
    if (GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        res = gpio_hold_en((gpio_num_t)pin);
        assert(res == ESP_OK);
    }
#endif
}

void enableLoraInterrupt()
{
    esp_err_t res;
    gpio_num_t pin;

    pin = GPIO_NUM_NC;

#if defined(LORA_DIO1) && (LORA_DIO1 != RADIOLIB_NC)
    pin = (gpio_num_t)LORA_DIO1;
#elif defined(RF95_IRQ) && (RF95_IRQ != RADIOLIB_NC)
    pin = (gpio_num_t)RF95_IRQ;
#endif

    if (pin == GPIO_NUM_NC) {
        return;
    }

#if defined(LORA_RESET) && (LORA_RESET != GPIO_NUM_NC)
    gpio_hold_en((gpio_num_t)LORA_RESET);
#endif

#if SOC_PM_SUPPORT_EXT_WAKEUP
    if (rtc_gpio_is_valid_gpio(pin)) {
        LOG_DEBUG("Setup radio interrupt (GPIO%02d) with wakeup by ext0 source", pin);
        res = rtc_gpio_pulldown_en((gpio_num_t)pin);
        assert(res == ESP_OK);
        res = rtc_gpio_hold_en((gpio_num_t)pin);
        assert(res == ESP_OK);
        res = esp_sleep_enable_ext0_wakeup(pin, HIGH);
        assert(res == ESP_OK);
    } else {
        LOG_DEBUG("Setup radio interrupt (GPIO%02d) with wakeup by GPIO interrupt", pin);
        res = gpio_pulldown_en((gpio_num_t)pin);
        assert(res == ESP_OK);
        res = gpio_wakeup_enable(pin, GPIO_INTR_HIGH_LEVEL);
        assert(res == ESP_OK);
        if (GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
            res = gpio_hold_en((gpio_num_t)pin);
            assert(res == ESP_OK);
        }
    }
#else
    LOG_DEBUG("Setup radio interrupt (GPIO%02d) with wakeup by GPIO interrupt", pin);
    res = gpio_pulldown_en((gpio_num_t)pin);
    assert(res == ESP_OK);
    res = gpio_wakeup_enable(pin, GPIO_INTR_HIGH_LEVEL);
    assert(res == ESP_OK);
    if (GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        res = gpio_hold_en((gpio_num_t)pin);
        assert(res == ESP_OK);
    }
#endif
}

bool shouldLoraWake(uint32_t msecToWake)
{
    return msecToWake < portMAX_DELAY && (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER);
}
#endif
