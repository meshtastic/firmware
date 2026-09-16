#if HAS_LORA_FEM
#include "LoRaFEMInterface.h"

#if defined(ARCH_ESP32)
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#endif

LoRaFEMInterface loraFEMInterface;

static void enableFEMPower()
{
    bool wasOff = digitalRead(LORA_PA_POWER) != HIGH;
    digitalWrite(LORA_PA_POWER, HIGH);
    if (wasOff) {
        delay(5); // This is an arbitrary 5ms for FEM rail power-up.
    }
}

#if defined(ARCH_ESP32)
static void releasePinHold(int pin)
{
    if (pin < 0) {
        return;
    }

    gpio_num_t gpio = (gpio_num_t)pin;

#if SOC_RTCIO_HOLD_SUPPORTED
    if (rtc_gpio_is_valid_gpio(gpio)) {
        rtc_gpio_hold_dis(gpio);
        return;
    }
#endif
    if (GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
        gpio_hold_dis(gpio);
    }
}

static void releaseSleepHolds()
{
    releasePinHold(LORA_PA_POWER);
#ifdef HELTEC_V4
    releasePinHold(LORA_KCT8103L_PA_CSD);
    releasePinHold(LORA_KCT8103L_PA_CTX);
#elif defined(USE_GC1109_PA)
    releasePinHold(LORA_GC1109_PA_EN);
    releasePinHold(LORA_GC1109_PA_TX_EN);
#elif defined(USE_KCT8103L_PA)
    releasePinHold(LORA_KCT8103L_PA_CSD);
    releasePinHold(LORA_KCT8103L_PA_CTX);
#endif
}
#endif

void LoRaFEMInterface::init(void)
{
    setLnaCanControl(false); // Default is uncontrollable
#if defined(RF_PA_DETECT_PIN)
    pinMode(RF_PA_DETECT_PIN, INPUT);
    high_power_pa = (digitalRead(RF_PA_DETECT_PIN) == RF_PA_HIGH_POWER_VALUE);
    LOG_INFO("Detected %s LoRa PA profile", high_power_pa ? "high-power" : "low-power");
#endif
#ifdef HELTEC_V4
    pinMode(LORA_PA_POWER, OUTPUT);
    digitalWrite(LORA_PA_POWER, HIGH);
    rtc_gpio_hold_dis((gpio_num_t)LORA_PA_POWER);
    delay(1);
    rtc_gpio_hold_dis((gpio_num_t)LORA_KCT8103L_PA_CSD);
    pinMode(LORA_KCT8103L_PA_CSD, INPUT); // detect which FEM is used
    delay(1);
    if (digitalRead(LORA_KCT8103L_PA_CSD) == HIGH) {
        // FEM is KCT8103L
        fem_type = KCT8103L_PA;
        LOG_INFO("Detected KCT8103L LoRa FEM");
        rtc_gpio_hold_dis((gpio_num_t)LORA_KCT8103L_PA_CTX);
        pinMode(LORA_KCT8103L_PA_CSD, OUTPUT);
        digitalWrite(LORA_KCT8103L_PA_CSD, HIGH);
        pinMode(LORA_KCT8103L_PA_CTX, OUTPUT);
        digitalWrite(LORA_KCT8103L_PA_CTX, LOW); // LNA enabled by default
        setLnaCanControl(true);
    } else if (digitalRead(LORA_KCT8103L_PA_CSD) == LOW) {
        // FEM is GC1109
        fem_type = GC1109_PA;
        LOG_INFO("Detected GC1109 LoRa FEM");
        // LORA_GC1109_PA_EN and LORA_KCT8103L_PA_CSD are the same pin and do not need to be repeatedly turned off and held.
        //  rtc_gpio_hold_dis((gpio_num_t)LORA_GC1109_PA_EN);
        pinMode(LORA_GC1109_PA_EN, OUTPUT);
        digitalWrite(LORA_GC1109_PA_EN, HIGH);
        pinMode(LORA_GC1109_PA_TX_EN, OUTPUT);
        digitalWrite(LORA_GC1109_PA_TX_EN, LOW);
    } else {
        fem_type = OTHER_FEM_TYPES;
    }
#elif defined(USE_GC1109_PA)
    fem_type = GC1109_PA;
    LOG_INFO("Using GC1109 LoRa FEM");
    pinMode(LORA_PA_POWER, OUTPUT);
    digitalWrite(LORA_PA_POWER, HIGH);
#if defined(ARCH_ESP32)
    rtc_gpio_hold_dis((gpio_num_t)LORA_PA_POWER);
    rtc_gpio_hold_dis((gpio_num_t)LORA_GC1109_PA_EN);
    rtc_gpio_hold_dis((gpio_num_t)LORA_GC1109_PA_TX_EN);
#endif
    delay(1);
    pinMode(LORA_GC1109_PA_EN, OUTPUT);
    digitalWrite(LORA_GC1109_PA_EN, HIGH);
    pinMode(LORA_GC1109_PA_TX_EN, OUTPUT);
    digitalWrite(LORA_GC1109_PA_TX_EN, LOW);
#elif defined(USE_KCT8103L_PA)
    fem_type = KCT8103L_PA;
    LOG_INFO("Using KCT8103L LoRa FEM");
    pinMode(LORA_PA_POWER, OUTPUT);
    digitalWrite(LORA_PA_POWER, HIGH);
#if defined(ARCH_ESP32)
    rtc_gpio_hold_dis((gpio_num_t)LORA_PA_POWER);
    rtc_gpio_hold_dis((gpio_num_t)LORA_KCT8103L_PA_CSD);
    rtc_gpio_hold_dis((gpio_num_t)LORA_KCT8103L_PA_CTX);
#endif
    delay(1);
    pinMode(LORA_KCT8103L_PA_CSD, OUTPUT);
    digitalWrite(LORA_KCT8103L_PA_CSD, HIGH);
    pinMode(LORA_KCT8103L_PA_CTX, OUTPUT);
    digitalWrite(LORA_KCT8103L_PA_CTX, LOW); // LNA enabled by default
    setLnaCanControl(true);
#elif defined(USE_KCT8103L_PA_ONLY)
    fem_type = KCT8103L_PA;
    pinMode(LORA_KCT8103L_EN, OUTPUT);
    digitalWrite(LORA_KCT8103L_EN, HIGH);
    delay(1);
    pinMode(LORA_KCT8103L_TX_RX, OUTPUT);
    digitalWrite(LORA_KCT8103L_TX_RX, LOW);
#endif
}

void LoRaFEMInterface::setSleepModeEnable(void)
{
#if defined(ARCH_ESP32)
    releaseSleepHolds();
#endif

#ifdef HELTEC_V4
    if (fem_type == GC1109_PA) {
        /*
         * Do not switch the power on and off frequently.
         * After turning off LORA_GC1109_PA_EN, the power consumption has dropped to the uA level.
         */
        digitalWrite(LORA_GC1109_PA_EN, LOW);
        digitalWrite(LORA_GC1109_PA_TX_EN, LOW);
    } else if (fem_type == KCT8103L_PA) {
        // shutdown the PA
        digitalWrite(LORA_KCT8103L_PA_CSD, LOW);
        digitalWrite(LORA_PA_POWER, LOW);
    }
#elif defined(USE_GC1109_PA)
    digitalWrite(LORA_GC1109_PA_EN, LOW);
    digitalWrite(LORA_GC1109_PA_TX_EN, LOW);
#elif defined(USE_KCT8103L_PA)
    // shutdown the PA
    digitalWrite(LORA_KCT8103L_PA_CSD, LOW);
    digitalWrite(LORA_PA_POWER, LOW);
#elif defined(USE_KCT8103L_PA_ONLY)
    // shutdown the PA
    digitalWrite(LORA_KCT8103L_EN, LOW);
#endif
}

void LoRaFEMInterface::setTxModeEnable(void)
{
#if defined(ARCH_ESP32)
    releaseSleepHolds();
#endif

#ifdef HELTEC_V4
    if (fem_type == GC1109_PA) {
        digitalWrite(LORA_GC1109_PA_EN, HIGH);    // CSD=1: Chip enabled
        digitalWrite(LORA_GC1109_PA_TX_EN, HIGH); // CPS: 1=full PA, 0=bypass (for RX, CPS is don't care)
    } else if (fem_type == KCT8103L_PA) {
        enableFEMPower();
        digitalWrite(LORA_KCT8103L_PA_CSD, HIGH);
        digitalWrite(LORA_KCT8103L_PA_CTX, HIGH);
    }
#elif defined(USE_GC1109_PA)
    digitalWrite(LORA_GC1109_PA_EN, HIGH);    // CSD=1: Chip enabled
    digitalWrite(LORA_GC1109_PA_TX_EN, HIGH); // CPS: 1=full PA, 0=bypass (for RX, CPS is don't care)
#elif defined(USE_KCT8103L_PA)
    enableFEMPower();
    digitalWrite(LORA_KCT8103L_PA_CSD, HIGH);
    digitalWrite(LORA_KCT8103L_PA_CTX, HIGH);
#elif defined(USE_KCT8103L_PA_ONLY)
    enableFEMPower();
    digitalWrite(LORA_KCT8103L_TX_RX, HIGH);
#endif
}

void LoRaFEMInterface::setRxModeEnable(void)
{
#if defined(ARCH_ESP32)
    releaseSleepHolds();
#endif

#ifdef HELTEC_V4
    if (fem_type == GC1109_PA) {
        digitalWrite(LORA_GC1109_PA_EN, HIGH); // CSD=1: Chip enabled
        digitalWrite(LORA_GC1109_PA_TX_EN, LOW);
    } else if (fem_type == KCT8103L_PA) {
        enableFEMPower();
        digitalWrite(LORA_KCT8103L_PA_CSD, HIGH);
        if (lna_enabled) {
            digitalWrite(LORA_KCT8103L_PA_CTX, LOW);
        } else {
            digitalWrite(LORA_KCT8103L_PA_CTX, HIGH);
        }
    }
#elif defined(USE_GC1109_PA)
    digitalWrite(LORA_GC1109_PA_EN, HIGH);    // CSD=1: Chip enabled
    digitalWrite(LORA_GC1109_PA_TX_EN, LOW);
#elif defined(USE_KCT8103L_PA)
    enableFEMPower();
    digitalWrite(LORA_KCT8103L_PA_CSD, HIGH);
    if (lna_enabled) {
        digitalWrite(LORA_KCT8103L_PA_CTX, LOW);
    } else {
        digitalWrite(LORA_KCT8103L_PA_CTX, HIGH);
    }
#elif defined(USE_KCT8103L_PA_ONLY)
    enableFEMPower();
    digitalWrite(LORA_KCT8103L_TX_RX, LOW);
#endif
}

void LoRaFEMInterface::setRxModeEnableWhenMCUSleep(void)
{
#if defined(ARCH_ESP32)
    releaseSleepHolds();
#endif

#ifdef HELTEC_V4
    // Keep FEM rail powered during deep sleep so LoRa RX wake can work (GC1109 keeps LNA active; KCT8103L uses RX bypass).
    // Set PA_POWER HIGH (overrides SX126xInterface::sleep() shutdown), then latch with RTC hold so the state survives deep sleep.
    enableFEMPower();
    rtc_gpio_hold_en((gpio_num_t)LORA_PA_POWER);
    if (fem_type == GC1109_PA) {
        digitalWrite(LORA_GC1109_PA_EN, HIGH);
        rtc_gpio_hold_en((gpio_num_t)LORA_GC1109_PA_EN);
        gpio_pulldown_en((gpio_num_t)LORA_GC1109_PA_TX_EN);
    } else if (fem_type == KCT8103L_PA) {
        digitalWrite(LORA_KCT8103L_PA_CSD, HIGH);
        rtc_gpio_hold_en((gpio_num_t)LORA_KCT8103L_PA_CSD);
        digitalWrite(LORA_KCT8103L_PA_CTX, HIGH); // RX bypass while MCU sleeps
        rtc_gpio_hold_en((gpio_num_t)LORA_KCT8103L_PA_CTX);
    }
#elif defined(USE_GC1109_PA)
    enableFEMPower();
    digitalWrite(LORA_GC1109_PA_EN, HIGH);
#if defined(ARCH_ESP32)
    rtc_gpio_hold_en((gpio_num_t)LORA_PA_POWER);
    rtc_gpio_hold_en((gpio_num_t)LORA_GC1109_PA_EN);
    gpio_pulldown_en((gpio_num_t)LORA_GC1109_PA_TX_EN);
#endif
#elif defined(USE_KCT8103L_PA)
    enableFEMPower();
    digitalWrite(LORA_KCT8103L_PA_CSD, HIGH);
    digitalWrite(LORA_KCT8103L_PA_CTX, HIGH); // RX bypass while MCU sleeps
#if defined(ARCH_ESP32)
    rtc_gpio_hold_en((gpio_num_t)LORA_PA_POWER);
    rtc_gpio_hold_en((gpio_num_t)LORA_KCT8103L_PA_CSD);
    rtc_gpio_hold_en((gpio_num_t)LORA_KCT8103L_PA_CTX);
#endif
#elif defined(USE_KCT8103L_PA_ONLY)
    enableFEMPower();
    digitalWrite(LORA_KCT8103L_TX_RX, LOW);
#endif
}

void LoRaFEMInterface::setLNAEnable(bool enabled)
{
    lna_enabled = enabled;
}

int8_t LoRaFEMInterface::powerConversion(int8_t loraOutputPower)
{
#if defined(RF_PA_DETECT_PIN)
    if (!high_power_pa) {
        return loraOutputPower;
    }
#endif
#ifdef HELTEC_V4
    const uint16_t gc1109_tx_gain[] = {11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 10, 10, 9, 9, 8, 7};
    const uint16_t kct8103l_tx_gain[] = {13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 12, 12, 11, 11, 10, 9, 8, 7};
    const uint16_t *tx_gain;
    uint16_t tx_gain_num;
    if (fem_type == GC1109_PA) {
        tx_gain = gc1109_tx_gain;
        tx_gain_num = sizeof(gc1109_tx_gain) / sizeof(gc1109_tx_gain[0]);
    } else if (fem_type == KCT8103L_PA) {
        tx_gain = kct8103l_tx_gain;
        tx_gain_num = sizeof(kct8103l_tx_gain) / sizeof(kct8103l_tx_gain[0]);
    } else {
        return loraOutputPower;
    }
#else
#ifdef ARCH_PORTDUINO
    const uint16_t *tx_gain = portduino_config.tx_gain_lora;
    uint16_t tx_gain_num = portduino_config.num_pa_points;
#else
    const uint16_t tx_gain[NUM_PA_POINTS] = {TX_GAIN_LORA};
    uint16_t tx_gain_num = NUM_PA_POINTS;
#endif
#endif
    for (int radio_dbm = 0; radio_dbm < tx_gain_num; radio_dbm++) {
        if (((radio_dbm + tx_gain[radio_dbm]) > loraOutputPower) ||
            ((radio_dbm == (tx_gain_num - 1)) && ((radio_dbm + tx_gain[radio_dbm]) <= loraOutputPower))) {
            // we've exceeded the power limit, or hit the max we can do
            LOG_INFO("Requested Tx power: %d dBm; Device LoRa Tx gain: %d dB", loraOutputPower, tx_gain[radio_dbm]);
            loraOutputPower -= tx_gain[radio_dbm];
            break;
        }
    }
    return loraOutputPower;
}

#endif
