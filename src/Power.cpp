/**
 * @file Power.cpp
 * @brief This file contains the implementation of the Power class, which is responsible for managing power-related functionality
 * of the device. It includes battery level sensing, power management unit (PMU) control, and power state machine management. The
 * Power class is used by the main device class to manage power-related functionality.
 *
 * The file also includes implementations of various battery level sensors, such as the AnalogBatteryLevel class, which assumes
 * the battery voltage is attached via a voltage-divider to an analog input.
 *
 * This file is part of the Meshtastic project.
 * For more information, see: https://meshtastic.org/
 */
#include "power.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "Throttle.h"
#include "buzz/buzz.h"
#include "configuration.h"
#include "main.h"
#include "meshUtils.h"
#include "sleep.h"

#if defined(ARCH_PORTDUINO)
#include "api/WiFiServerAPI.h"
#include "input/LinuxInputImpl.h"
#endif

// Working USB detection for powered/charging states on the RAK platform
#ifdef NRF_APM
#include "nrfx_power.h"
#endif

#if defined(DEBUG_HEAP_MQTT) && !MESHTASTIC_EXCLUDE_MQTT
#include "mqtt/MQTT.h"
#include "target_specific.h"
#if HAS_WIFI
#include <WiFi.h>
#endif

#if HAS_ETHERNET && defined(USE_WS5500)
#include <ETHClass2.h>
#define ETH ETH2
#endif // HAS_ETHERNET

#endif

#ifndef DELAY_FOREVER
#define DELAY_FOREVER portMAX_DELAY
#endif

#if defined(BATTERY_PIN) && defined(ARCH_ESP32)

#ifndef BAT_MEASURE_ADC_UNIT // ADC1 is default
static const adc1_channel_t adc_channel = ADC_CHANNEL;
static const adc_unit_t unit = ADC_UNIT_1;
#else // ADC2
static const adc2_channel_t adc_channel = ADC_CHANNEL;
static const adc_unit_t unit = ADC_UNIT_2;
RTC_NOINIT_ATTR uint64_t RTC_reg_b;

#endif // BAT_MEASURE_ADC_UNIT

esp_adc_cal_characteristics_t *adc_characs = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
#ifndef ADC_ATTENUATION
static const adc_atten_t atten = ADC_ATTEN_DB_12;
#else
static const adc_atten_t atten = ADC_ATTENUATION;
#endif
#endif // BATTERY_PIN && ARCH_ESP32

#ifdef EXT_CHRG_DETECT
#ifndef EXT_CHRG_DETECT_MODE
static const uint8_t ext_chrg_detect_mode = INPUT;
#else
static const uint8_t ext_chrg_detect_mode = EXT_CHRG_DETECT_MODE;
#endif
#ifndef EXT_CHRG_DETECT_VALUE
static const uint8_t ext_chrg_detect_value = HIGH;
#else
static const uint8_t ext_chrg_detect_value = EXT_CHRG_DETECT_VALUE;
#endif
#endif

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
#if __has_include(<Adafruit_INA219.h>)
INA219Sensor ina219Sensor;
#else
NullSensor ina219Sensor;
#endif

#if __has_include(<INA226.h>)
INA226Sensor ina226Sensor;
#else
NullSensor ina226Sensor;
#endif

#if __has_include(<Adafruit_INA260.h>)
INA260Sensor ina260Sensor;
#else
NullSensor ina260Sensor;
#endif

#if __has_include(<INA3221.h>)
INA3221Sensor ina3221Sensor;
#else
NullSensor ina3221Sensor;
#endif

#endif

#if !MESHTASTIC_EXCLUDE_I2C
#include "modules/Telemetry/Sensor/MAX17048Sensor.h"
#include <utility>
extern std::pair<uint8_t, TwoWire *> nodeTelemetrySensorsMap[_meshtastic_TelemetrySensorType_MAX + 1];
#if HAS_TELEMETRY && (!MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_POWER_TELEMETRY)
#if __has_include(<Adafruit_MAX1704X.h>)
MAX17048Sensor max17048Sensor;
#else
NullSensor max17048Sensor;
#endif
#endif
#endif

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && HAS_RAKPROT
RAK9154Sensor rak9154Sensor;
#endif

#ifdef HAS_PPM
// note: XPOWERS_CHIP_XXX must be defined in variant.h
#include <XPowersLib.h>
XPowersPPM *PPM = NULL;
#endif

#ifdef HAS_BQ27220
#include "bq27220.h"
#endif

#ifdef HAS_PMU
XPowersLibInterface *PMU = NULL;
#else

// Copy of the base class defined in axp20x.h.
// I'd rather not include axp20x.h as it brings Wire dependency.
class HasBatteryLevel
{
  public:
    /**
     * Battery state of charge, from 0 to 100 or -1 for unknown
     */
    virtual int getBatteryPercent() { return -1; }

    /**
     * The raw voltage of the battery or NAN if unknown
     */
    virtual uint16_t getBattVoltage() { return 0; }

    /**
     * return true if there is a battery installed in this unit
     */
    virtual bool isBatteryConnect() { return false; }

    virtual bool isVbusIn() { return false; }
    virtual bool isCharging() { return false; }
};
#endif

bool pmu_irq = false;

Power *power;

using namespace meshtastic;

#ifndef AREF_VOLTAGE
#if defined(ARCH_NRF52)
/*
 * Internal Reference is +/-0.6V, with an adjustable gain of 1/6, 1/5, 1/4,
 * 1/3, 1/2 or 1, meaning 3.6, 3.0, 2.4, 1.8, 1.2 or 0.6V for the ADC levels.
 *
 * External Reference is VDD/4, with an adjustable gain of 1, 2 or 4, meaning
 * VDD/4, VDD/2 or VDD for the ADC levels.
 *
 * Default settings are internal reference with 1/6 gain (GND..3.6V ADC range)
 */
#define AREF_VOLTAGE 3.6
#else
#define AREF_VOLTAGE 3.3
#endif
#endif

/**
 * If this board has a battery level sensor, set this to a valid implementation
 */
static HasBatteryLevel *batteryLevel; // Default to NULL for no battery level sensor

#ifdef BATTERY_PIN

void battery_adcEnable()
{
#ifdef ADC_CTRL // enable adc voltage divider when we need to read
#ifdef ADC_USE_PULLUP
    pinMode(ADC_CTRL, INPUT_PULLUP);
#else
#ifdef HELTEC_V3
    pinMode(ADC_CTRL, INPUT);
    uint8_t adc_ctl_enable_value = !(digitalRead(ADC_CTRL));
    pinMode(ADC_CTRL, OUTPUT);
    digitalWrite(ADC_CTRL, adc_ctl_enable_value);
#else
    pinMode(ADC_CTRL, OUTPUT);
    digitalWrite(ADC_CTRL, ADC_CTRL_ENABLED);
#endif
#endif
    delay(10);
#endif
}

static void battery_adcDisable()
{
#ifdef ADC_CTRL // disable adc voltage divider when we need to read
#ifdef ADC_USE_PULLUP
    pinMode(ADC_CTRL, INPUT_PULLDOWN);
#else
#ifdef HELTEC_V3
    pinMode(ADC_CTRL, ANALOG);
#else
    digitalWrite(ADC_CTRL, !ADC_CTRL_ENABLED);
#endif
#endif
#endif
}

#endif

/**
 * A simple battery level sensor that assumes the battery voltage is attached via a voltage-divider to an analog input
 */
class AnalogBatteryLevel : public HasBatteryLevel
{
  public:
    /**
     * Battery state of charge, from 0 to 100 or -1 for unknown
     */
    virtual int getBatteryPercent() override
    {
#if defined(HAS_RAKPROT) && !defined(HAS_PMU)
        if (hasRAK()) {
            return rak9154Sensor.getBusBatteryPercent();
        }
#endif

        float v = getBattVoltage();

        if (v < noBatVolt)
            return -1; // If voltage is super low assume no battery installed

#ifdef NO_BATTERY_LEVEL_ON_CHARGE
        // This does not work on a RAK4631 with battery connected
        if (v > chargingVolt)
            return 0; // While charging we can't report % full on the battery
#endif
        /**
         * @brief   Battery voltage lookup table interpolation to obtain a more
         * precise percentage rather than the old proportional one.
         * @author  Gabriele Russo
         * @date    06/02/2024
         */
        float battery_SOC = 0.0;
        uint16_t voltage = v / NUM_CELLS; // single cell voltage (average)
        for (int i = 0; i < NUM_OCV_POINTS; i++) {
            if (OCV[i] <= voltage) {
                if (i == 0) {
                    battery_SOC = 100.0; // 100% full
                } else {
                    // interpolate between OCV[i] and OCV[i-1]
                    battery_SOC = (float)100.0 / (NUM_OCV_POINTS - 1.0) *
                                  (NUM_OCV_POINTS - 1.0 - i + ((float)voltage - OCV[i]) / (OCV[i - 1] - OCV[i]));
                }
                break;
            }
        }
        return clamp((int)(battery_SOC), 0, 100);
    }

    /**
     * The raw voltage of the batteryin millivolts or NAN if unknown
     */
    virtual uint16_t getBattVoltage() override
    {

#if HAS_TELEMETRY && defined(HAS_RAKPROT) && !defined(HAS_PMU) && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
        if (hasRAK()) {
            return getRAKVoltage();
        }
#endif

#if HAS_TELEMETRY && !defined(HAS_PMU) && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
        if (hasINA()) {
            return getINAVoltage();
        }
#endif

#ifndef ADC_MULTIPLIER
#define ADC_MULTIPLIER 2.0
#endif

#ifndef BATTERY_SENSE_SAMPLES
#define BATTERY_SENSE_SAMPLES                                                                                                    \
    15 // Set the number of samples, it has an effect of increasing sensitivity in complex electromagnetic environment.
#endif

#ifdef BATTERY_PIN
        // Override variant or default ADC_MULTIPLIER if we have the override pref
        float operativeAdcMultiplier =
            config.power.adc_multiplier_override > 0 ? config.power.adc_multiplier_override : ADC_MULTIPLIER;
        // Do not call analogRead() often.
        const uint32_t min_read_interval = 5000;
        if (!initial_read_done || !Throttle::isWithinTimespanMs(last_read_time_ms, min_read_interval)) {
            last_read_time_ms = millis();

            uint32_t raw = 0;
            float scaled = 0;

            battery_adcEnable();
#ifdef ARCH_ESP32 // ADC block for espressif platforms
            raw = espAdcRead();
            scaled = esp_adc_cal_raw_to_voltage(raw, adc_characs);
            scaled *= operativeAdcMultiplier;
#else // block for all other platforms
            for (uint32_t i = 0; i < BATTERY_SENSE_SAMPLES; i++) {
                raw += analogRead(BATTERY_PIN);
            }
            raw = raw / BATTERY_SENSE_SAMPLES;
            scaled = operativeAdcMultiplier * ((1000 * AREF_VOLTAGE) / pow(2, BATTERY_SENSE_RESOLUTION_BITS)) * raw;
#endif
            battery_adcDisable();

            if (!initial_read_done) {
                // Flush the smoothing filter with an ADC reading, if the reading is plausibly correct
                if (scaled > last_read_value)
                    last_read_value = scaled;
                initial_read_done = true;
            } else {
                // Already initialized - filter this reading
                last_read_value += (scaled - last_read_value) * 0.5; // Virtual LPF
            }

            // LOG_DEBUG("battery gpio %d raw val=%u scaled=%u filtered=%u", BATTERY_PIN, raw, (uint32_t)(scaled), (uint32_t)
            // (last_read_value));
        }
        return last_read_value;
#endif // BATTERY_PIN
        return 0;
    }

#if defined(ARCH_ESP32) && !defined(HAS_PMU) && defined(BATTERY_PIN)
    /**
     * ESP32 specific function for getting calibrated ADC reads
     */
    uint32_t espAdcRead()
    {

        uint32_t raw = 0;
        uint8_t raw_c = 0; // raw reading counter

#ifndef BAT_MEASURE_ADC_UNIT // ADC1
        for (int i = 0; i < BATTERY_SENSE_SAMPLES; i++) {
            int val_ = adc1_get_raw(adc_channel);
            if (val_ >= 0) { // save only valid readings
                raw += val_;
                raw_c++;
            }
            // delayMicroseconds(100);
        }
#else                            // ADC2
#ifdef CONFIG_IDF_TARGET_ESP32S3 // ESP32S3
        // ADC2 wifi bug workaround not required, breaks compile
        // On ESP32S3, ADC2 can take turns with Wifi (?)

        int32_t adc_buf;
        esp_err_t read_result;

        // Multiple samples
        for (int i = 0; i < BATTERY_SENSE_SAMPLES; i++) {
            adc_buf = 0;
            read_result = -1;

            read_result = adc2_get_raw(adc_channel, ADC_WIDTH_BIT_12, &adc_buf);
            if (read_result == ESP_OK) {
                raw += adc_buf;
                raw_c++; // Count valid samples
            } else {
                LOG_DEBUG("An attempt to sample ADC2 failed");
            }
        }

#else  // Other ESP32
        int32_t adc_buf = 0;
        for (int i = 0; i < BATTERY_SENSE_SAMPLES; i++) {
            // ADC2 wifi bug workaround, see
            // https://github.com/espressif/arduino-esp32/issues/102
            WRITE_PERI_REG(SENS_SAR_READ_CTRL2_REG, RTC_reg_b);
            SET_PERI_REG_MASK(SENS_SAR_READ_CTRL2_REG, SENS_SAR2_DATA_INV);
            adc2_get_raw(adc_channel, ADC_WIDTH_BIT_12, &adc_buf);
            raw += adc_buf;
            raw_c++;
        }
#endif // BAT_MEASURE_ADC_UNIT

#endif // End BAT_MEASURE_ADC_UNIT
        return (raw / (raw_c < 1 ? 1 : raw_c));
    }
#endif

    /**
     * return true if there is a battery installed in this unit
     */
    // if we have a integrated device with a battery, we can assume that the battery is always connected
#ifdef BATTERY_IMMUTABLE
    virtual bool isBatteryConnect() override { return true; }
#elif defined(ADC_V)
    virtual bool isBatteryConnect() override
    {
        int lastReading = digitalRead(ADC_V);
        // 判断值是否变化
        for (int i = 2; i < 500; i++) {
            int reading = digitalRead(ADC_V);
            if (reading != lastReading) {
                return false; // 有变化，USB供电, 没接电池
            }
        }

        return true;
    }
#else
    virtual bool isBatteryConnect() override { return getBatteryPercent() != -1; }
#endif

    /// If we see a battery voltage higher than physics allows - assume charger is pumping
    /// in power
    /// On some boards we don't have the power management chip (like AXPxxxx)
    /// so we use EXT_PWR_DETECT GPIO pin to detect external power source
    virtual bool isVbusIn() override
    {
#ifdef EXT_PWR_DETECT
#if defined(HELTEC_CAPSULE_SENSOR_V3) || defined(HELTEC_SENSOR_HUB)
        // if external powered that pin will be pulled down
        if (digitalRead(EXT_PWR_DETECT) == LOW) {
            return true;
        }
        // if it's not LOW - check the battery
#else
        // if external powered that pin will be pulled up
        if (digitalRead(EXT_PWR_DETECT) == HIGH) {
            return true;
        }
        // if it's not HIGH - check the battery
#endif
#endif
        return getBattVoltage() > chargingVolt;
    }

    /// Assume charging if we have a battery and external power is connected.
    /// we can't be smart enough to say 'full'?
    virtual bool isCharging() override
    {
#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HAS_RAKPROT) && !defined(HAS_PMU)
        if (hasRAK()) {
            return (rak9154Sensor.isCharging()) ? OptTrue : OptFalse;
        }
#endif
#ifdef EXT_CHRG_DETECT
        return digitalRead(EXT_CHRG_DETECT) == ext_chrg_detect_value;
#else
#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && !defined(DISABLE_INA_CHARGING_DETECTION)
        if (hasINA()) {
            // get current flow from INA sensor - negative value means power flowing into the battery
            // default assuming  BATTERY+  <--> INA_VIN+ <--> SHUNT RESISTOR <--> INA_VIN- <--> LOAD
            LOG_DEBUG("Using INA on I2C addr 0x%x for charging detection", config.power.device_battery_ina_address);
#if defined(INA_CHARGING_DETECTION_INVERT)
            return getINACurrent() > 0;
#else
            return getINACurrent() < 0;
#endif
        }
        return isBatteryConnect() && isVbusIn();
#endif
#endif
        // by default, we check the battery voltage only
        return isVbusIn();
    }

  private:
    /// If we see a battery voltage higher than physics allows - assume charger is pumping
    /// in power

    /// For heltecs with no battery connected, the measured voltage is 2204, so
    // need to be higher than that, in this case is 2500mV (3000-500)
    const uint16_t OCV[NUM_OCV_POINTS] = {OCV_ARRAY};
    const float chargingVolt = (OCV[0] + 10) * NUM_CELLS;
    const float noBatVolt = (OCV[NUM_OCV_POINTS - 1] - 500) * NUM_CELLS;
    // Start value from minimum voltage for the filter to not start from 0
    // that could trigger some events.
    // This value is over-written by the first ADC reading, it the voltage seems reasonable.
    bool initial_read_done = false;
    float last_read_value = (OCV[NUM_OCV_POINTS - 1] * NUM_CELLS);
    uint32_t last_read_time_ms = 0;

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HAS_RAKPROT)

    uint16_t getRAKVoltage() { return rak9154Sensor.getBusVoltageMv(); }

    bool hasRAK()
    {
        if (!rak9154Sensor.isInitialized())
            return rak9154Sensor.runOnce() > 0;
        return rak9154Sensor.isRunning();
    }
#endif

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
    uint16_t getINAVoltage()
    {
        if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA219].first == config.power.device_battery_ina_address) {
            return ina219Sensor.getBusVoltageMv();
        } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA226].first ==
                   config.power.device_battery_ina_address) {
            return ina226Sensor.getBusVoltageMv();
        } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA260].first ==
                   config.power.device_battery_ina_address) {
            return ina260Sensor.getBusVoltageMv();
        } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA3221].first ==
                   config.power.device_battery_ina_address) {
            return ina3221Sensor.getBusVoltageMv();
        }
        return 0;
    }

    int16_t getINACurrent()
    {
        if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA219].first == config.power.device_battery_ina_address) {
            return ina219Sensor.getCurrentMa();
        } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA226].first ==
                   config.power.device_battery_ina_address) {
            return ina226Sensor.getCurrentMa();
        } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA3221].first ==
                   config.power.device_battery_ina_address) {
            return ina3221Sensor.getCurrentMa();
        }
        return 0;
    }

    bool hasINA()
    {
        if (!config.power.device_battery_ina_address) {
            return false;
        }
        if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA219].first == config.power.device_battery_ina_address) {
            if (!ina219Sensor.isInitialized())
                return ina219Sensor.runOnce() > 0;
            return ina219Sensor.isRunning();
        } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA226].first ==
                   config.power.device_battery_ina_address) {
            if (!ina226Sensor.isInitialized())
                return ina226Sensor.runOnce() > 0;
            return ina226Sensor.isRunning();
        } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA260].first ==
                   config.power.device_battery_ina_address) {
            if (!ina260Sensor.isInitialized())
                return ina260Sensor.runOnce() > 0;
            return ina260Sensor.isRunning();
        } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA3221].first ==
                   config.power.device_battery_ina_address) {
            if (!ina3221Sensor.isInitialized())
                return ina3221Sensor.runOnce() > 0;
            return ina3221Sensor.isRunning();
        }
        return false;
    }
#endif
};

static AnalogBatteryLevel analogLevel;

Power::Power() : OSThread("Power")
{
    statusHandler = {};
    low_voltage_counter = 0;
#ifdef DEBUG_HEAP
    lastheap = memGet.getFreeHeap();
#endif
}

bool Power::analogInit()
{
#ifdef EXT_PWR_DETECT
#if defined(HELTEC_CAPSULE_SENSOR_V3) || defined(HELTEC_SENSOR_HUB)
    pinMode(EXT_PWR_DETECT, INPUT_PULLUP);
#else
    pinMode(EXT_PWR_DETECT, INPUT);
#endif
#endif
#ifdef EXT_CHRG_DETECT
    pinMode(EXT_CHRG_DETECT, ext_chrg_detect_mode);
#endif

#ifdef BATTERY_PIN
    LOG_DEBUG("Use analog input %d for battery level", BATTERY_PIN);

    // disable any internal pullups
    pinMode(BATTERY_PIN, INPUT);

#ifndef BATTERY_SENSE_RESOLUTION_BITS
#define BATTERY_SENSE_RESOLUTION_BITS 10
#endif

#ifdef ARCH_ESP32 // ESP32 needs special analog stuff

#ifndef ADC_WIDTH // max resolution by default
    static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
#else
    static const adc_bits_width_t width = ADC_WIDTH;
#endif
#ifndef BAT_MEASURE_ADC_UNIT // ADC1
    adc1_config_width(width);
    adc1_config_channel_atten(adc_channel, atten);
#else // ADC2
    adc2_config_channel_atten(adc_channel, atten);
#ifndef CONFIG_IDF_TARGET_ESP32S3
    // ADC2 wifi bug workaround
    // Not required with ESP32S3, breaks compile
    RTC_reg_b = READ_PERI_REG(SENS_SAR_READ_CTRL2_REG);
#endif
#endif
    // calibrate ADC
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_characs);
    // show ADC characterization base
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        LOG_INFO("ADC config based on Two Point values stored in eFuse");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        LOG_INFO("ADC config based on reference voltage stored in eFuse");
    }
#ifdef CONFIG_IDF_TARGET_ESP32S3
    // ESP32S3
    else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP_FIT) {
        LOG_INFO("ADC config based on Two Point values and fitting curve coefficients stored in eFuse");
    }
#endif
    else {
        LOG_INFO("ADC config based on default reference voltage");
    }
#endif // ARCH_ESP32

#ifdef ARCH_NRF52
#ifdef VBAT_AR_INTERNAL
    analogReference(VBAT_AR_INTERNAL);
#else
    analogReference(AR_INTERNAL); // 3.6V
#endif
#endif // ARCH_NRF52

#ifndef ARCH_ESP32
    analogReadResolution(BATTERY_SENSE_RESOLUTION_BITS);
#endif

    batteryLevel = &analogLevel;
    return true;
#else
    return false;
#endif
}

/**
 * Initializes the Power class.
 *
 * @return true if the setup was successful, false otherwise.
 */
bool Power::setup()
{
    bool found = false;
    if (axpChipInit()) {
        found = true;
    } else if (lipoInit()) {
        found = true;
    } else if (lipoChargerInit()) {
        found = true;
    } else if (meshSolarInit()) {
        found = true;
    } else if (analogInit()) {
        found = true;
    }

#ifdef NRF_APM
    found = true;
#endif
#ifdef EXT_PWR_DETECT
    attachInterrupt(
        EXT_PWR_DETECT,
        []() {
            power->setIntervalFromNow(0);
            runASAP = true;
            BaseType_t higherWake = 0;
        },
        CHANGE);
#endif

    enabled = found;
    low_voltage_counter = 0;

    return found;
}

void Power::powerCommandsCheck()
{
    if (rebootAtMsec && millis() > rebootAtMsec) {
        LOG_INFO("Rebooting");
        reboot();
    }

    if (shutdownAtMsec && millis() > shutdownAtMsec) {
        shutdownAtMsec = 0;
        shutdown();
    }
}

void Power::reboot()
{
    notifyReboot.notifyObservers(NULL);
#if defined(ARCH_ESP32)
    ESP.restart();
#elif defined(ARCH_NRF52)
    NVIC_SystemReset();
#elif defined(ARCH_RP2040)
    rp2040.reboot();
#elif defined(ARCH_PORTDUINO)
    deInitApiServer();
    if (aLinuxInputImpl)
        aLinuxInputImpl->deInit();
    SPI.end();
    Wire.end();
    Serial1.end();
    if (screen) {
        delete screen;
        screen = nullptr;
    }
    LOG_DEBUG("final reboot!");
    ::reboot();
#elif defined(ARCH_STM32WL)
    HAL_NVIC_SystemReset();
#else
    rebootAtMsec = -1;
    LOG_WARN("FIXME implement reboot for this platform. Note that some settings require a restart to be applied");
#endif
}

void Power::shutdown()
{

#if HAS_SCREEN
    if (screen) {
#ifdef T_DECK_PRO
        screen->showSimpleBanner("Device is powered off.\nConnect USB to start!", 0); // T-Deck Pro has no power button
#else
        screen->showSimpleBanner("Shutting Down...", 0); // stays on screen
#endif
    }
#endif
#if !defined(ARCH_STM32WL)
    playShutdownMelody();
#endif
    nodeDB->saveToDisk();

#if defined(ARCH_NRF52) || defined(ARCH_ESP32) || defined(ARCH_RP2040)
#ifdef PIN_LED1
    ledOff(PIN_LED1);
#endif
#ifdef PIN_LED2
    ledOff(PIN_LED2);
#endif
#ifdef PIN_LED3
    ledOff(PIN_LED3);
#endif
    doDeepSleep(DELAY_FOREVER, true, true);
#elif defined(ARCH_PORTDUINO)
    exit(EXIT_SUCCESS);
#else
    LOG_WARN("FIXME implement shutdown for this platform");
#endif
}

/// Reads power status to powerStatus singleton.
//
// TODO(girts): move this and other axp stuff to power.h/power.cpp.
void Power::readPowerStatus()
{
    int32_t batteryVoltageMv = -1; // Assume unknown
    int8_t batteryChargePercent = -1;
    OptionalBool usbPowered = OptUnknown;
    OptionalBool hasBattery = OptUnknown; // These must be static because NRF_APM code doesn't run every time
    OptionalBool isChargingNow = OptUnknown;

    if (batteryLevel) {
        hasBattery = batteryLevel->isBatteryConnect() ? OptTrue : OptFalse;
        usbPowered = batteryLevel->isVbusIn() ? OptTrue : OptFalse;
        isChargingNow = batteryLevel->isCharging() ? OptTrue : OptFalse;
        if (hasBattery) {
            batteryVoltageMv = batteryLevel->getBattVoltage();
            // If the AXP192 returns a valid battery percentage, use it
            if (batteryLevel->getBatteryPercent() >= 0) {
                batteryChargePercent = batteryLevel->getBatteryPercent();
            } else {
                // If the AXP192 returns a percentage less than 0, the feature is either not supported or there is an error
                // In that case, we compute an estimate of the charge percent based on open circuit voltage table defined
                // in power.h
                batteryChargePercent = clamp((int)(((batteryVoltageMv - (OCV[NUM_OCV_POINTS - 1] * NUM_CELLS)) * 1e2) /
                                                   ((OCV[0] * NUM_CELLS) - (OCV[NUM_OCV_POINTS - 1] * NUM_CELLS))),
                                             0, 100);
            }
        }
    }

// FIXME: IMO we shouldn't be littering our code with all these ifdefs.  Way better instead to make a Nrf52IsUsbPowered subclass
// (which shares a superclass with the BatteryLevel stuff)
// that just provides a few methods.  But in the interest of fixing this bug I'm going to follow current
// practice.
#ifdef NRF_APM // Section of code detects USB power on the RAK4631 and updates the power states.  Takes 20 seconds or so to detect
               // changes.

    nrfx_power_usb_state_t nrf_usb_state = nrfx_power_usbstatus_get();
    // LOG_DEBUG("NRF Power %d", nrf_usb_state);

    // If changed to DISCONNECTED
    if (nrf_usb_state == NRFX_POWER_USB_STATE_DISCONNECTED)
        isChargingNow = usbPowered = OptFalse;
    // If changed to CONNECTED / READY
    else
        isChargingNow = usbPowered = OptTrue;

#endif

    // Notify any status instances that are observing us
    const PowerStatus powerStatus2 = PowerStatus(hasBattery, usbPowered, isChargingNow, batteryVoltageMv, batteryChargePercent);
    if (millis() > lastLogTime + 50 * 1000) {
        LOG_DEBUG("Battery: usbPower=%d, isCharging=%d, batMv=%d, batPct=%d", powerStatus2.getHasUSB(),
                  powerStatus2.getIsCharging(), powerStatus2.getBatteryVoltageMv(), powerStatus2.getBatteryChargePercent());
        lastLogTime = millis();
    }
    newStatus.notifyObservers(&powerStatus2);
#ifdef DEBUG_HEAP
    if (lastheap != memGet.getFreeHeap()) {
        // Use stack-allocated buffer to avoid heap allocations in monitoring code
        char threadlist[256] = "Threads running:";
        int threadlistLen = strlen(threadlist);
        int running = 0;
        for (int i = 0; i < MAX_THREADS; i++) {
            auto thread = concurrency::mainController.get(i);
            if ((thread != nullptr) && (thread->enabled)) {
                // Use snprintf to safely append to stack buffer without heap allocation
                int remaining = sizeof(threadlist) - threadlistLen - 1;
                if (remaining > 0) {
                    int written = snprintf(threadlist + threadlistLen, remaining, " %s", thread->ThreadName.c_str());
                    if (written > 0 && written < remaining) {
                        threadlistLen += written;
                    }
                }
                running++;
            }
        }
        LOG_HEAP(threadlist);
        LOG_HEAP("Heap status: %d/%d bytes free (%d), running %d/%d threads", memGet.getFreeHeap(), memGet.getHeapSize(),
                 memGet.getFreeHeap() - lastheap, running, concurrency::mainController.size(false));
        lastheap = memGet.getFreeHeap();
    }
#ifdef DEBUG_HEAP_MQTT
    if (mqtt) {
        // send MQTT-Packet with Heap-Size
        uint8_t dmac[6];
        getMacAddr(dmac); // Get our hardware ID
        char mac[18];
        sprintf(mac, "!%02x%02x%02x%02x", dmac[2], dmac[3], dmac[4], dmac[5]);

        auto newHeap = memGet.getFreeHeap();
        // Use stack-allocated buffers to avoid heap allocations in monitoring code
        char heapTopic[128];
        snprintf(heapTopic, sizeof(heapTopic), "%s/2/heap/%s", (*moduleConfig.mqtt.root ? moduleConfig.mqtt.root : "msh"), mac);
        char heapString[16];
        snprintf(heapString, sizeof(heapString), "%u", newHeap);
        mqtt->pubSub.publish(heapTopic, heapString, false);

        auto wifiRSSI = WiFi.RSSI();
        char wifiTopic[128];
        snprintf(wifiTopic, sizeof(wifiTopic), "%s/2/wifi/%s", (*moduleConfig.mqtt.root ? moduleConfig.mqtt.root : "msh"), mac);
        char wifiString[16];
        snprintf(wifiString, sizeof(wifiString), "%d", wifiRSSI);
        mqtt->pubSub.publish(wifiTopic, wifiString, false);
    }
#endif

#endif

    // If we have a battery at all and it is less than 0%, force deep sleep if we have more than 10 low readings in
    // a row. NOTE: min LiIon/LiPo voltage is 2.0 to 2.5V, current OCV min is set to 3100 that is large enough.
    //

    if (batteryLevel && powerStatus2.getHasBattery() && !powerStatus2.getHasUSB()) {
        if (batteryLevel->getBattVoltage() < OCV[NUM_OCV_POINTS - 1]) {
            low_voltage_counter++;
            LOG_DEBUG("Low voltage counter: %d/10", low_voltage_counter);
            if (low_voltage_counter > 10) {
                LOG_INFO("Low voltage detected, trigger deep sleep");
                powerFSM.trigger(EVENT_LOW_BATTERY);
            }
        } else {
            low_voltage_counter = 0;
        }
    }
}

int32_t Power::runOnce()
{
    readPowerStatus();

#ifdef HAS_PMU
    // WE no longer use the IRQ line to wake the CPU (due to false wakes from sleep), but we do poll
    // the IRQ status by reading the registers over I2C
    if (PMU) {

        PMU->getIrqStatus();

        if (PMU->isVbusRemoveIrq()) {
            LOG_INFO("USB unplugged");
            powerFSM.trigger(EVENT_POWER_DISCONNECTED);
        }

        if (PMU->isVbusInsertIrq()) {
            LOG_INFO("USB plugged In");
            powerFSM.trigger(EVENT_POWER_CONNECTED);
        }

        /*
        Other things we could check if we cared...

        if (PMU->isBatChagerStartIrq()) {
            LOG_DEBUG("Battery start charging");
        }
        if (PMU->isBatChagerDoneIrq()) {
            LOG_DEBUG("Battery fully charged");
        }
        if (PMU->isBatInsertIrq()) {
            LOG_DEBUG("Battery inserted");
        }
        if (PMU->isBatRemoveIrq()) {
            LOG_DEBUG("Battery removed");
        }
        */
#ifndef T_WATCH_S3 // FIXME - why is this triggering on the T-Watch S3?
        if (PMU->isPekeyLongPressIrq()) {
            LOG_DEBUG("PEK long button press");
            if (screen)
                screen->setOn(false);
        }
#endif

        PMU->clearIrqStatus();
    }
#endif
    // Only read once every 20 seconds once the power status for the app has been initialized
    return (statusHandler && statusHandler->isInitialized()) ? (1000 * 20) : RUN_SAME;
}

/**
 * Init the power manager chip
 *
 * axp192 power
    DCDC1 0.7-3.5V @ 1200mA max -> OLED // If you turn this off you'll lose comms to the axp192 because the OLED and the
 axp192 share the same i2c bus, instead use ssd1306 sleep mode DCDC2 -> unused DCDC3 0.7-3.5V @ 700mA max -> ESP32 (keep this
 on!) LDO1 30mA -> charges GPS backup battery // charges the tiny J13 battery by the GPS to power the GPS ram (for a couple of
 days), can not be turned off LDO2 200mA -> LORA LDO3 200mA -> GPS
 *
 */
bool Power::axpChipInit()
{

#ifdef HAS_PMU

    TwoWire *w = NULL;

    // Use macro to distinguish which wire is used by PMU
#ifdef PMU_USE_WIRE1
    w = &Wire1;
#else
    w = &Wire;
#endif

    /**
     * It is not necessary to specify the wire pin,
     * just input the wire, because the wire has been initialized in main.cpp
     */
    if (!PMU) {
        PMU = new XPowersAXP2101(*w);
        if (!PMU->init()) {
            LOG_WARN("No AXP2101 power management");
            delete PMU;
            PMU = NULL;
        } else {
            LOG_INFO("AXP2101 PMU init succeeded");
        }
    }

    if (!PMU) {
        PMU = new XPowersAXP192(*w);
        if (!PMU->init()) {
            LOG_WARN("No AXP192 power management");
            delete PMU;
            PMU = NULL;
        } else {
            LOG_INFO("AXP192 PMU init succeeded");
        }
    }

    if (!PMU) {
        /*
         * In XPowersLib, if the XPowersAXPxxx object is released, Wire.end() will be called at the same time.
         * In order not to affect other devices, if the initialization of the PMU fails, Wire needs to be re-initialized once,
         * if there are multiple devices sharing the bus.
         * * */
#ifndef PMU_USE_WIRE1
        w->begin(I2C_SDA, I2C_SCL);
#endif
        return false;
    }

    batteryLevel = PMU;

    if (PMU->getChipModel() == XPOWERS_AXP192) {

        // lora radio power channel
        PMU->setPowerChannelVoltage(XPOWERS_LDO2, 3300);
        PMU->enablePowerOutput(XPOWERS_LDO2);

        // oled module power channel,
        // disable it will cause abnormal communication between boot and AXP power supply,
        // do not turn it off
        PMU->setPowerChannelVoltage(XPOWERS_DCDC1, 3300);
        // enable oled power
        PMU->enablePowerOutput(XPOWERS_DCDC1);

        // gnss module power channel -  now turned on in setGpsPower
        PMU->setPowerChannelVoltage(XPOWERS_LDO3, 3300);
        // PMU->enablePowerOutput(XPOWERS_LDO3);

        // protected oled power source
        PMU->setProtectedChannel(XPOWERS_DCDC1);
        // protected esp32 power source
        PMU->setProtectedChannel(XPOWERS_DCDC3);

        // disable not use channel
        PMU->disablePowerOutput(XPOWERS_DCDC2);

        // disable all axp chip interrupt
        PMU->disableIRQ(XPOWERS_AXP192_ALL_IRQ);

        // Set constant current charging current
        PMU->setChargerConstantCurr(XPOWERS_AXP192_CHG_CUR_450MA);

        // Set up the charging voltage
        PMU->setChargeTargetVoltage(XPOWERS_AXP192_CHG_VOL_4V2);
    } else if (PMU->getChipModel() == XPOWERS_AXP2101) {

        /*The alternative version of T-Beam 1.1 differs from T-Beam V1.1 in that it uses an AXP2101 power chip*/
        if (HW_VENDOR == meshtastic_HardwareModel_TBEAM) {
            // Unuse power channel
            PMU->disablePowerOutput(XPOWERS_DCDC2);
            PMU->disablePowerOutput(XPOWERS_DCDC3);
            PMU->disablePowerOutput(XPOWERS_DCDC4);
            PMU->disablePowerOutput(XPOWERS_DCDC5);
            PMU->disablePowerOutput(XPOWERS_ALDO1);
            PMU->disablePowerOutput(XPOWERS_ALDO4);
            PMU->disablePowerOutput(XPOWERS_BLDO1);
            PMU->disablePowerOutput(XPOWERS_BLDO2);
            PMU->disablePowerOutput(XPOWERS_DLDO1);
            PMU->disablePowerOutput(XPOWERS_DLDO2);

            // GNSS RTC PowerVDD 3300mV
            PMU->setPowerChannelVoltage(XPOWERS_VBACKUP, 3300);
            PMU->enablePowerOutput(XPOWERS_VBACKUP);

            // ESP32 VDD 3300mV
            //  ! No need to set, automatically open , Don't close it
            //  PMU->setPowerChannelVoltage(XPOWERS_DCDC1, 3300);
            //  PMU->setProtectedChannel(XPOWERS_DCDC1);

            // LoRa VDD 3300mV
            PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);
            PMU->enablePowerOutput(XPOWERS_ALDO2);

            // GNSS VDD 3300mV
            PMU->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);
            PMU->enablePowerOutput(XPOWERS_ALDO3);
        } else if (HW_VENDOR == meshtastic_HardwareModel_LILYGO_TBEAM_S3_CORE ||
                   HW_VENDOR == meshtastic_HardwareModel_T_WATCH_S3) {
            // t-beam s3 core
            /**
             * gnss module power channel
             * The default ALDO4 is off, you need to turn on the GNSS power first, otherwise it will be invalid during
             * initialization
             */
            PMU->setPowerChannelVoltage(XPOWERS_ALDO4, 3300);
            PMU->enablePowerOutput(XPOWERS_ALDO4);

            // lora radio power channel
            PMU->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);
            PMU->enablePowerOutput(XPOWERS_ALDO3);

            // m.2 interface
            PMU->setPowerChannelVoltage(XPOWERS_DCDC3, 3300);
            PMU->enablePowerOutput(XPOWERS_DCDC3);

            /**
             * ALDO2 cannot be turned off.
             * It is a necessary condition for sensor communication.
             * It must be turned on to properly access the sensor and screen
             * It is also responsible for the power supply of PCF8563
             */
            PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);
            PMU->enablePowerOutput(XPOWERS_ALDO2);

            // 6-axis , magnetometer ,bme280 , oled screen power channel
            PMU->setPowerChannelVoltage(XPOWERS_ALDO1, 3300);
            PMU->enablePowerOutput(XPOWERS_ALDO1);

            // sdcard power channel
            PMU->setPowerChannelVoltage(XPOWERS_BLDO1, 3300);
            PMU->enablePowerOutput(XPOWERS_BLDO1);

#ifdef T_WATCH_S3
            // DRV2605 power channel
            PMU->setPowerChannelVoltage(XPOWERS_BLDO2, 3300);
            PMU->enablePowerOutput(XPOWERS_BLDO2);
#endif

            // PMU->setPowerChannelVoltage(XPOWERS_DCDC4, 3300);
            // PMU->enablePowerOutput(XPOWERS_DCDC4);

            // not use channel
            PMU->disablePowerOutput(XPOWERS_DCDC2); // not elicited
            PMU->disablePowerOutput(XPOWERS_DCDC5); // not elicited
            PMU->disablePowerOutput(XPOWERS_DLDO1); // Invalid power channel, it does not exist
            PMU->disablePowerOutput(XPOWERS_DLDO2); // Invalid power channel, it does not exist
            PMU->disablePowerOutput(XPOWERS_VBACKUP);
        }

        // disable all axp chip interrupt
        PMU->disableIRQ(XPOWERS_AXP2101_ALL_IRQ);

        // Set the constant current charging current of AXP2101, temporarily use 500mA by default
        PMU->setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);

        // Set up the charging voltage
        PMU->setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    }

    PMU->clearIrqStatus();

    // TBeam1.1 /T-Beam S3-Core has no external TS detection,
    // it needs to be disabled, otherwise it will cause abnormal charging
    PMU->disableTSPinMeasure();

    // PMU->enableSystemVoltageMeasure();
    PMU->enableVbusVoltageMeasure();
    PMU->enableBattVoltageMeasure();

    if (PMU->isChannelAvailable(XPOWERS_DCDC1)) {
        LOG_DEBUG("DC1  : %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_DCDC1) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_DCDC1));
    }
    if (PMU->isChannelAvailable(XPOWERS_DCDC2)) {
        LOG_DEBUG("DC2  : %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_DCDC2) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_DCDC2));
    }
    if (PMU->isChannelAvailable(XPOWERS_DCDC3)) {
        LOG_DEBUG("DC3  : %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_DCDC3) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_DCDC3));
    }
    if (PMU->isChannelAvailable(XPOWERS_DCDC4)) {
        LOG_DEBUG("DC4  : %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_DCDC4) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_DCDC4));
    }
    if (PMU->isChannelAvailable(XPOWERS_LDO2)) {
        LOG_DEBUG("LDO2 : %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_LDO2) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_LDO2));
    }
    if (PMU->isChannelAvailable(XPOWERS_LDO3)) {
        LOG_DEBUG("LDO3 : %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_LDO3) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_LDO3));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO1)) {
        LOG_DEBUG("ALDO1: %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_ALDO1) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_ALDO1));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO2)) {
        LOG_DEBUG("ALDO2: %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_ALDO2) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_ALDO2));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO3)) {
        LOG_DEBUG("ALDO3: %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_ALDO3) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_ALDO3));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO4)) {
        LOG_DEBUG("ALDO4: %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_ALDO4) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_ALDO4));
    }
    if (PMU->isChannelAvailable(XPOWERS_BLDO1)) {
        LOG_DEBUG("BLDO1: %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_BLDO1) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_BLDO1));
    }
    if (PMU->isChannelAvailable(XPOWERS_BLDO2)) {
        LOG_DEBUG("BLDO2: %s   Voltage:%u mV ", PMU->isPowerChannelEnable(XPOWERS_BLDO2) ? "+" : "-",
                  PMU->getPowerChannelVoltage(XPOWERS_BLDO2));
    }

// We can safely ignore this approach for most (or all) boards because MCU turned off
// earlier than battery discharged to 2.6V.
//
// Unfortanly for now we can't use this killswitch for RAK4630-based boards because they have a bug with
// battery voltage measurement. Probably it sometimes drops to low values.
#ifndef RAK4630
    // Set PMU shutdown voltage at 2.6V to maximize battery utilization
    PMU->setSysPowerDownVoltage(2600);
#endif

#ifdef PMU_IRQ
    uint64_t pmuIrqMask = 0;

    if (PMU->getChipModel() == XPOWERS_AXP192) {
        pmuIrqMask = XPOWERS_AXP192_VBUS_INSERT_IRQ | XPOWERS_AXP192_BAT_INSERT_IRQ | XPOWERS_AXP192_PKEY_SHORT_IRQ;
    } else if (PMU->getChipModel() == XPOWERS_AXP2101) {
        pmuIrqMask = XPOWERS_AXP2101_VBUS_INSERT_IRQ | XPOWERS_AXP2101_BAT_INSERT_IRQ | XPOWERS_AXP2101_PKEY_SHORT_IRQ;
    }

    pinMode(PMU_IRQ, INPUT);
    attachInterrupt(
        PMU_IRQ, [] { pmu_irq = true; }, FALLING);

    // we do not look for AXPXXX_CHARGING_FINISHED_IRQ & AXPXXX_CHARGING_IRQ because it occurs repeatedly while there is
    // no battery also it could cause inadvertent waking from light sleep just because the battery filled
    // we don't look for AXPXXX_BATT_REMOVED_IRQ because it occurs repeatedly while no battery installed
    // we don't look at AXPXXX_VBUS_REMOVED_IRQ because we don't have anything hooked to vbus
    PMU->enableIRQ(pmuIrqMask);

    PMU->clearIrqStatus();
#endif /*PMU_IRQ*/

    readPowerStatus();

    pmu_found = true;

    return pmu_found;

#else
    return false;
#endif
}

#if !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_MAX1704X.h>)

/**
 * Wrapper class for an I2C MAX17048 Lipo battery sensor.
 */
class LipoBatteryLevel : public HasBatteryLevel
{
  private:
    MAX17048Singleton *max17048 = nullptr;

  public:
    /**
     * Init the I2C MAX17048 Lipo battery level sensor
     */
    bool runOnce()
    {
        if (max17048 == nullptr) {
            max17048 = MAX17048Singleton::GetInstance();
        }

        // try to start if the sensor has been detected
        if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_MAX17048].first != 0) {
            return max17048->runOnce(nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_MAX17048].second);
        }
        return false;
    }

    /**
     * Battery state of charge, from 0 to 100 or -1 for unknown
     */
    virtual int getBatteryPercent() override { return max17048->getBusBatteryPercent(); }

    /**
     * The raw voltage of the battery in millivolts, or NAN if unknown
     */
    virtual uint16_t getBattVoltage() override { return max17048->getBusVoltageMv(); }

    /**
     * return true if there is a battery installed in this unit
     */
    virtual bool isBatteryConnect() override { return max17048->isBatteryConnected(); }

    /**
     * return true if there is an external power source detected
     */
    virtual bool isVbusIn() override { return max17048->isExternallyPowered(); }

    /**
     * return true if the battery is currently charging
     */
    virtual bool isCharging() override { return max17048->isBatteryCharging(); }
};

LipoBatteryLevel lipoLevel;

/**
 * Init the Lipo battery level sensor
 */
bool Power::lipoInit()
{
    bool result = lipoLevel.runOnce();
    LOG_DEBUG("Power::lipoInit lipo sensor is %s", result ? "ready" : "not ready yet");
    if (!result)
        return false;
    batteryLevel = &lipoLevel;
    return true;
}

#else
/**
 * The Lipo battery level sensor is unavailable - default to AnalogBatteryLevel
 */
bool Power::lipoInit()
{
    return false;
}
#endif

#if defined(HAS_PPM) && HAS_PPM

/**
 * Adapter class for BQ25896/BQ27220 Lipo battery charger.
 */
class LipoCharger : public HasBatteryLevel
{
  private:
    BQ27220 *bq = nullptr;

  public:
    /**
     * Init the I2C BQ25896 Lipo battery charger
     */
    bool runOnce()
    {
        if (PPM == nullptr) {
            PPM = new XPowersPPM;
            bool result = PPM->init(Wire, I2C_SDA, I2C_SCL, BQ25896_ADDR);
            if (result) {
                LOG_INFO("PPM BQ25896 init succeeded");
                // Set the minimum operating voltage. Below this voltage, the PPM will protect
                // PPM->setSysPowerDownVoltage(3100);

                // Set input current limit, default is 500mA
                // PPM->setInputCurrentLimit(800);

                // Disable current limit pin
                // PPM->disableCurrentLimitPin();

                // Set the charging target voltage, Range:3840 ~ 4608mV ,step:16 mV
                PPM->setChargeTargetVoltage(4288);

                // Set the precharge current , Range: 64mA ~ 1024mA ,step:64mA
                // PPM->setPrechargeCurr(64);

                // The premise is that limit pin is disabled, or it will
                // only follow the maximum charging current set by limit pin.
                // Set the charging current , Range:0~5056mA ,step:64mA
                PPM->setChargerConstantCurr(1024);

                // To obtain voltage data, the ADC must be enabled first
                PPM->enableMeasure();

                // Turn on charging function
                // If there is no battery connected, do not turn on the charging function
                PPM->enableCharge();
            } else {
                LOG_WARN("PPM BQ25896 init failed");
                delete PPM;
                PPM = nullptr;
                return false;
            }
        }
        if (bq == nullptr) {
            bq = new BQ27220;
            bq->setDefaultCapacity(BQ27220_DESIGN_CAPACITY);

            bool result = bq->init();
            if (result) {
                LOG_DEBUG("BQ27220 design capacity: %d", bq->getDesignCapacity());
                LOG_DEBUG("BQ27220 fullCharge capacity: %d", bq->getFullChargeCapacity());
                LOG_DEBUG("BQ27220 remaining capacity: %d", bq->getRemainingCapacity());
                return true;
            } else {
                LOG_WARN("BQ27220 init failed");
                delete bq;
                bq = nullptr;
                return false;
            }
        }
        return false;
    }

    /**
     * Battery state of charge, from 0 to 100 or -1 for unknown
     */
    virtual int getBatteryPercent() override
    {
        return -1;
        // return bq->getChargePercent(); // don't use BQ27220 for battery percent, it is not calibrated
    }

    /**
     * The raw voltage of the battery in millivolts, or NAN if unknown
     */
    virtual uint16_t getBattVoltage() override { return bq->getVoltage(); }

    /**
     * return true if there is a battery installed in this unit
     */
    virtual bool isBatteryConnect() override { return PPM->getBattVoltage() > 0; }

    /**
     * return true if there is an external power source detected
     */
    virtual bool isVbusIn() override { return PPM->getVbusVoltage() > 0; }

    /**
     * return true if the battery is currently charging
     */
    virtual bool isCharging() override
    {
        bool isCharging = PPM->isCharging();
        if (isCharging) {
            LOG_DEBUG("BQ27220 time to full charge: %d min", bq->getTimeToFull());
        } else {
            if (!PPM->isVbusIn()) {
                LOG_DEBUG("BQ27220 time to empty: %d min (%d mAh)", bq->getTimeToEmpty(), bq->getRemainingCapacity());
            }
        }
        return isCharging;
    }
};

LipoCharger lipoCharger;

/**
 * Init the Lipo battery charger
 */
bool Power::lipoChargerInit()
{
    bool result = lipoCharger.runOnce();
    LOG_DEBUG("Power::lipoChargerInit lipo sensor is %s", result ? "ready" : "not ready yet");
    if (!result)
        return false;
    batteryLevel = &lipoCharger;
    return true;
}

#else
/**
 * The Lipo battery level sensor is unavailable - default to AnalogBatteryLevel
 */
bool Power::lipoChargerInit()
{
    return false;
}
#endif

#ifdef HELTEC_MESH_SOLAR
#include "meshSolarApp.h"

/**
 * meshSolar class for an SMBUS battery sensor.
 */
class meshSolarBatteryLevel : public HasBatteryLevel
{

  public:
    /**
     * Init the I2C meshSolar battery level sensor
     */
    bool runOnce()
    {
        meshSolarStart();
        return true;
    }

    /**
     * Battery state of charge, from 0 to 100 or -1 for unknown
     */
    virtual int getBatteryPercent() override { return meshSolarGetBatteryPercent(); }

    /**
     * The raw voltage of the battery in millivolts, or NAN if unknown
     */
    virtual uint16_t getBattVoltage() override { return meshSolarGetBattVoltage(); }

    /**
     * return true if there is a battery installed in this unit
     */
    virtual bool isBatteryConnect() override { return meshSolarIsBatteryConnect(); }

    /**
     * return true if there is an external power source detected
     */
    virtual bool isVbusIn() override { return meshSolarIsVbusIn(); }

    /**
     * return true if the battery is currently charging
     */
    virtual bool isCharging() override { return meshSolarIsCharging(); }
};

meshSolarBatteryLevel meshSolarLevel;

/**
 * Init the meshSolar battery level sensor
 */
bool Power::meshSolarInit()
{
    bool result = meshSolarLevel.runOnce();
    LOG_DEBUG("Power::meshSolarInit mesh solar sensor is %s", result ? "ready" : "not ready yet");
    if (!result)
        return false;
    batteryLevel = &meshSolarLevel;
    return true;
}

#else
/**
 * The meshSolar battery level sensor is unavailable - default to AnalogBatteryLevel
 */
bool Power::meshSolarInit()
{
    return false;
}
#endif
