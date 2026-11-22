#include "configuration.h"

#include "Observer.h"
#include "TDisplayP4Board.h"
#include "bluetooth/HostedBluetooth.h"
#include "esp_task_wdt.h"
#include "mesh/Throttle.h"
#include "platform/esp32/ExtensionIOXL9535.h"
#include "sleep.h"
#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

// Requested sleep time, stored by cpuDeepSleep() before it calls variant_shutdown()
extern uint32_t espDeepSleepRequestMs;

// On-chip LDOs, acquired once at boot and never released
static ldo_channel_handle_t ldoDsiPhy = nullptr; // LDO_VO3: MIPI D-PHY rail
static ldo_channel_handle_t ldoSdIo = nullptr;   // LDO_VO4: SD IO rail (GPIO39-48)
static esp_err_t ldoDsiPhyResult = ESP_FAIL;
static esp_err_t ldoSdIoResult = ESP_FAIL;

static bool expanderFound = false;     // XL9535 answered in earlyInitVariant()
static bool expanderBootState = false; // boot latches and directions were written and verified

static std::atomic<bool> i2cUsable{true};

// cpuDeepSleep() isolates the LP pads GPIO2 (LoRa SCK) and GPIO13, doDeepSleep() holds LORA_CS.
// TODO(hw-verify): do these pad holds survive a non-power-on reset? Releasing is harmless when not held.
static void releasePadHolds()
{
    static const gpio_num_t lpPads[] = {GPIO_NUM_2, GPIO_NUM_13};
    for (gpio_num_t pad : lpPads) {
        rtc_gpio_hold_dis(pad);
        rtc_gpio_deinit(pad);
    }
    gpio_hold_dis((gpio_num_t)LORA_CS);
}

// Runs before consoleInit(): no LOG_* in here (and none in the expander driver).
void earlyInitVariant()
{
    releasePadHolds();

    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);

    // TODO(hw-verify): 400 kHz is the vendor speed for the GT9895 on this bus; fall back to 100000 if unreliable
    Wire.begin(I2C_SDA, I2C_SCL, 400000);

    for (uint8_t attempt = 0; attempt < 5 && !expanderFound; attempt++) {
        if (attempt > 0)
            delay(20);
        expanderFound = xl9535IoExpander.begin(Wire, XL9535_ADDR);
    }

    // Latches first, then directions (inside writePorts): Low_3V3 stays on, radio and panel/touch
    // held in reset, C6 in reset, SD VDD off, internal antenna, GPS awake.
    for (uint8_t attempt = 0; expanderFound && attempt < 3 && !expanderBootState; attempt++)
        expanderBootState = xl9535IoExpander.writePorts(TDP4_XIO_BOOT_LATCH, TDP4_XIO_OUTPUT_MASK);

    // LilyGO order: expander, then LDO_VO3 (2.5 V) and LDO_VO4 (3.3 V), before any panel reset.
    // TODO(hw-verify): LilyGO acquires both with owned_by_hw/bypass; here they are plain fixed-voltage channels.
    ldoDsiPhyResult = ldoAcquireChannel(3, 2500, false, &ldoDsiPhy);
    ldoSdIoResult = ldoAcquireChannel(4, 3300, false, &ldoSdIo);

    if (expanderFound) {
        delay(20); // radio NRST low and SD VDD off since the boot latch
        xl9535IoExpander.digitalWrite(EXIO_LORA_NRST, HIGH);
        delay(10);
        xl9535IoExpander.digitalWrite(EXIO_SD_PWR_N, LOW); // SD VDD on, after about 30 ms off
    }

    pinMode(XL9535_INT_PIN, INPUT);
}

// Parks the peripherals for the soft-off. notifyDeepSleep fires before doDeepSleep() ends Wire.
struct TDisplayP4DeepSleepObserver {
    int onDeepSleep(void *)
    {
        i2cUsable = false; // first: the tft task must stop polling the touch controller
        xl9535IoExpander.digitalWrite(EXIO_DISP_RST, LOW);
        xl9535IoExpander.digitalWrite(EXIO_TOUCH_RST, LOW);
        xl9535IoExpander.digitalWrite(EXIO_C6_EN, LOW);
        xl9535IoExpander.digitalWrite(EXIO_LORA_NRST, LOW);
        xl9535IoExpander.digitalWrite(EXIO_AUDIO_PWR, LOW);
        xl9535IoExpander.digitalWrite(EXIO_GPS_WAKE, LOW);
        xl9535IoExpander.digitalWrite(EXIO_SD_PWR_N, HIGH);
        // EXIO_3V3_EN_N is left alone: Low_3V3 must stay on
        return 0;
    }
    CallbackObserver<TDisplayP4DeepSleepObserver, void *> observer{this, &TDisplayP4DeepSleepObserver::onDeepSleep};
} static deepSleepObserver;

void lateInitVariant()
{
    if (expanderFound) {
        uint16_t inputs = 0;
        const bool inputsRead = xl9535IoExpander.readInputs(inputs);
        LOG_INFO("XL9535 ready: out=0x%04x outputs=0x%04x in=0x%04x%s errors=%u", (unsigned)xl9535IoExpander.outputShadow(),
                 (unsigned)xl9535IoExpander.outputMaskShadow(), (unsigned)inputs, inputsRead ? "" : " (read failed)",
                 (unsigned)xl9535IoExpander.errorCount());
        if (!expanderBootState)
            LOG_ERROR("XL9535 boot state not verified: power, reset and antenna lines may be wrong");
    } else {
        LOG_ERROR("XL9535 not responding at 0x%02x: no LoRa, no display/touch reset, no BLE", XL9535_ADDR);
    }
    LOG_INFO("LDO_VO3 2500 mV: %s, LDO_VO4 3300 mV: %s", esp_err_to_name(ldoDsiPhyResult), esp_err_to_name(ldoSdIoResult));

    // Registered after the radio's own observer, so the radio is already asleep when its reset is pulled
    deepSleepObserver.observer.observe(&notifyDeepSleep);
}

// Deep sleep can leave the P4 locked in download mode on this board, so cpuDeepSleep() ends here:
// idle until RST / power cycle, or restart once a timed sleep has elapsed. I2C is already down.
void variant_shutdown()
{
    i2cUsable = false; // also covers timed sleeps that skip notifyDeepSleep
    releasePadHolds();

    const uint32_t startMs = millis();
    const bool feedWatchdog = esp_task_wdt_status(NULL) == ESP_OK;
    while (true) {
        if (feedWatchdog)
            esp_task_wdt_reset();
        delay(1000);
        if (espDeepSleepRequestMs != portMAX_DELAY && Throttle::hasElapsed(startMs, espDeepSleepRequestMs))
            ESP.restart();
    }
}

// The ESP32-C6 EN line is XL9535 IO14 (1 = run); esp_hosted only gets a dummy reset GPIO.
bool variantHostedSlaveReset(bool assertReset)
{
    if (!xl9535IoExpander.isReady()) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            LOG_ERROR("XL9535 not ready: cannot drive the ESP32-C6 EN line");
        }
        return true;
    }

    xl9535IoExpander.digitalWrite(EXIO_C6_EN, LOW);
    if (assertReset)
        return true;

    delay(50);
    xl9535IoExpander.digitalWrite(EXIO_C6_EN, HIGH);
    delay(50);
    // esp_hosted now pulses its dummy reset GPIO and waits CONFIG_ESP_HOSTED_SDIO_RESET_DELAY_MS (1500 ms).
    // TODO(hw-verify): add delay(1000) here if the SDIO card init fails (vendor waits 1000 ms after EN)
    return true;
}

void tDisplayP4PanelReset()
{
    xl9535IoExpander.digitalWrite(EXIO_DISP_RST, LOW);
    delay(10);
    xl9535IoExpander.digitalWrite(EXIO_DISP_RST, HIGH);
    delay(120);
}

void tDisplayP4TouchReset()
{
    xl9535IoExpander.digitalWrite(EXIO_TOUCH_RST, LOW);
    delay(30);
    xl9535IoExpander.digitalWrite(EXIO_TOUCH_RST, HIGH);
    delay(100);
}

bool tDisplayP4ExpanderReady()
{
    return xl9535IoExpander.isReady();
}

bool tDisplayP4I2cUsable()
{
    return i2cUsable.load();
}
