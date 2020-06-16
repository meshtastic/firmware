#include "sleep.h"
#include "GPS.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "configuration.h"
#include "error.h"

#include "main.h"
#include "target_specific.h"

#ifndef NO_ESP32
#include "esp32/pm.h"
#include "esp_pm.h"
#include "rom/rtc.h"
#include <driver/rtc_io.h>
#include <driver/uart.h>

#include "BluetoothUtil.h"

esp_sleep_source_t wakeCause; // the reason we booted this time
#endif

#ifdef TBEAM_V10
#include "axp20x.h"
extern AXP20X_Class axp;
#endif

/// Called to ask any observers if they want to veto sleep. Return 1 to veto or 0 to allow sleep to happen
Observable<void *> preflightSleep;

/// Called to tell observers we are now entering sleep and you should prepare.  Must return 0
/// notifySleep will be called for light or deep sleep, notifyDeepSleep is only called for deep sleep
Observable<void *> notifySleep, notifyDeepSleep;

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
#ifndef NO_ESP32
    setCpuFrequencyMhz(on ? 240 : 80);
#endif
}

void setLed(bool ledOn)
{
#ifdef LED_PIN
    // toggle the led so we can get some rough sense of how often loop is pausing
    digitalWrite(LED_PIN, ledOn ^ LED_INVERTED);
#endif

#ifdef TBEAM_V10
    if (axp192_found) {
        // blink the axp led
        axp.setChgLEDMode(ledOn ? AXP20X_LED_LOW_LEVEL : AXP20X_LED_OFF);
    }
#endif
}

void setGPSPower(bool on)
{
    DEBUG_MSG("Setting GPS power=%d\n", on);

#ifdef TBEAM_V10
    if (axp192_found)
        axp.setPowerOutPut(AXP192_LDO3, on ? AXP202_ON : AXP202_OFF); // GPS main power
#endif
}

// Perform power on init that we do on each wake from deep sleep
void initDeepSleep()
{
#ifndef NO_ESP32
    bootCount++;
    wakeCause = esp_sleep_get_wakeup_cause();
    /*
      Not using yet because we are using wake on all buttons being low

      wakeButtons = esp_sleep_get_ext1_wakeup_status();       // If one of these buttons is set it was the reason we woke
      if (wakeCause == ESP_SLEEP_WAKEUP_EXT1 && !wakeButtons) // we must have been using the 'all buttons rule for waking' to
      support busted boards, assume button one was pressed wakeButtons = ((uint64_t)1) << buttons.gpios[0];
      */

    // If we booted because our timer ran out or the user pressed reset, send those as fake events
    const char *reason = "reset"; // our best guess
    RESET_REASON hwReason = rtc_get_reset_reason(0);

    if (hwReason == RTCWDT_BROWN_OUT_RESET)
        reason = "brownout";

    if (hwReason == TG0WDT_SYS_RESET)
        reason = "taskWatchdog";

    if (hwReason == TG1WDT_SYS_RESET)
        reason = "intWatchdog";

    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER)
        reason = "timeout";

    DEBUG_MSG("booted, wake cause %d (boot count %d), reset_reason=%s\n", wakeCause, bootCount, reason);
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
static void waitEnterSleep()
{
    uint32_t now = millis();
    while (!doPreflightSleep()) {
        delay(100); // Kinda yucky - wait until radio says say we can shutdown (finished in process sends/receives)

        if (millis() - now > 30 * 1000) { // If we wait too long just report an error and go to sleep
            recordCriticalError(ErrSleepEnterWait);
            assert(0); // FIXME - for now we just restart, need to fix bug #167
            break;
        }
    }

    // Code that still needs to be moved into notifyObservers
    Serial.flush();            // send all our characters before we stop cpu clock
    setBluetoothEnable(false); // has to be off before calling light sleep

    notifySleep.notifyObservers(NULL);
}

void doDeepSleep(uint64_t msecToWake)
{
    DEBUG_MSG("Entering deep sleep for %llu seconds\n", msecToWake / 1000);

#ifndef NO_ESP32
    // not using wifi yet, but once we are this is needed to shutoff the radio hw
    // esp_wifi_stop();
    waitEnterSleep();
    notifySleep.notifyObservers(NULL); // also tell the regular sleep handlers
    notifyDeepSleep.notifyObservers(NULL);

    screen.setOn(false); // datasheet says this will draw only 10ua

    nodeDB.saveToDisk();

#ifdef RESET_OLED
    digitalWrite(RESET_OLED, 1); // put the display in reset before killing its power
#endif

#ifdef VEXT_ENABLE
    digitalWrite(VEXT_ENABLE, 1); // turn off the display power
#endif

    setLed(false);

#ifdef TBEAM_V10
    if (axp192_found) {
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
                                       12,           13,
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
#endif
}

#ifndef NO_ESP32
/**
 * enter light sleep (preserves ram but stops everything about CPU).
 *
 * Returns (after restoring hw state) when the user presses a button or we get a LoRa interrupt
 */
esp_sleep_wakeup_cause_t doLightSleep(uint64_t sleepMsec) // FIXME, use a more reasonable default
{
    // DEBUG_MSG("Enter light sleep\n");

    waitEnterSleep();

    uint64_t sleepUsec = sleepMsec * 1000LL;

    // NOTE! ESP docs say we must disable bluetooth and wifi before light sleep

    // We want RTC peripherals to stay on
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

#ifdef BUTTON_NEED_PULLUP
    gpio_pullup_en((gpio_num_t)BUTTON_PIN);
#endif

#ifdef SERIAL0_RX_GPIO
    // We treat the serial port as a GPIO for a fast/low power way of waking, if we see a rising edge that means
    // someone started to send something

    // Alas - doesn't work reliably, instead need to use the uart specific version (which burns a little power)
    // FIXME: gpio 3 is RXD for serialport 0 on ESP32
    // Send a few Z characters to wake the port
    gpio_wakeup_enable((gpio_num_t)SERIAL0_RX_GPIO, GPIO_INTR_LOW_LEVEL);
    // uart_set_wakeup_threshold(UART_NUM_0, 3);
    // esp_sleep_enable_uart_wakeup(0);
#endif
#ifdef BUTTON_PIN
    gpio_wakeup_enable((gpio_num_t)BUTTON_PIN, GPIO_INTR_LOW_LEVEL); // when user presses, this button goes low
#endif
#ifdef RF95_IRQ_GPIO
    gpio_wakeup_enable((gpio_num_t)RF95_IRQ_GPIO, GPIO_INTR_HIGH_LEVEL); // RF95 interrupt, active high
#endif
#ifdef PMU_IRQ
    // FIXME, disable wake due to PMU because it seems to fire all the time?
    if (axp192_found)
        gpio_wakeup_enable((gpio_num_t)PMU_IRQ, GPIO_INTR_LOW_LEVEL); // pmu irq
#endif
    assert(esp_sleep_enable_gpio_wakeup() == ESP_OK);
    assert(esp_sleep_enable_timer_wakeup(sleepUsec) == ESP_OK);
    assert(esp_light_sleep_start() == ESP_OK);

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_GPIO)
        DEBUG_MSG("Exit light sleep gpio: btn=%d\n", !digitalRead(BUTTON_PIN));

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
  static esp_pm_config_esp32_t config; // filled with zeros because bss

  config.max_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ;
  config.min_freq_mhz = 20; // 10Mhz is minimum recommended
  config.light_sleep_enable = false;
  DEBUG_MSG("Sleep request result %x\n", esp_pm_configure(&config));
}
#endif
