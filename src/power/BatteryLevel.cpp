#include "power/BatteryLevel.h"

#include "NodeDB.h"
#include "Throttle.h"
#include "meshUtils.h"
#include "power/PowerHAL.h"

#if defined(ARCH_NRF52)
#include "Nrf52SaadcLock.h"
#include "concurrency/LockGuard.h"
#endif

#if defined(HAS_BQ27220)
#include "bq27220.h"
#endif

#if defined(HAS_PPM) && HAS_PPM
#include <XPowersLib.h>
#endif

#ifdef HELTEC_MESH_SOLAR
#include "meshSolarApp.h"
#endif

#if !MESHTASTIC_EXCLUDE_I2C
#include <Wire.h>
#include <utility>
extern std::pair<uint8_t, TwoWire *> nodeTelemetrySensorsMap[_meshtastic_TelemetrySensorType_MAX + 1];
#endif

#ifndef ADC_MULTIPLIER
#define ADC_MULTIPLIER 2.0
#endif

#ifndef BATTERY_SENSE_SAMPLES
#define BATTERY_SENSE_SAMPLES 15
#endif

#if !defined(AREF_VOLTAGE) && !defined(ARCH_NRF52)
#define AREF_VOLTAGE 3.3
#endif

#if defined(BATTERY_PIN) && defined(ARCH_ESP32)

#ifndef BAT_MEASURE_ADC_UNIT
static const adc_channel_t adc_channel = ADC_CHANNEL;
static const adc_unit_t unit = ADC_UNIT_1;
#else
static const adc_channel_t adc_channel = ADC_CHANNEL;
static const adc_unit_t unit = ADC_UNIT_2;
#endif

static adc_oneshot_unit_handle_t adc_handle = nullptr;
static adc_cali_handle_t adc_cali_handle = nullptr;
static bool adc_calibrated = false;

#ifndef ADC_ATTENUATION
static const adc_atten_t atten = ADC_ATTEN_DB_12;
#else
static const adc_atten_t atten = ADC_ATTENUATION;
#endif

#ifdef ADC_BITWIDTH
static const adc_bitwidth_t adc_width = ADC_BITWIDTH;
#else
static const adc_bitwidth_t adc_width = ADC_BITWIDTH_DEFAULT;
#endif

static int adcBitWidthToBits(adc_bitwidth_t width)
{
    switch (width) {
    case ADC_BITWIDTH_9:
        return 9;
    case ADC_BITWIDTH_10:
        return 10;
    case ADC_BITWIDTH_11:
        return 11;
    case ADC_BITWIDTH_12:
        return 12;
#ifdef ADC_BITWIDTH_13
    case ADC_BITWIDTH_13:
        return 13;
#endif
    default:
        return 12;
    }
}

static bool initAdcCalibration()
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = adc_width,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
    if (ret == ESP_OK) {
        LOG_INFO("ADC calibration: curve fitting enabled");
        return true;
    }
    if (ret != ESP_ERR_NOT_SUPPORTED) {
        LOG_WARN("ADC calibration: curve fitting failed: %s", esp_err_to_name(ret));
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = adc_width,
        .default_vref = DEFAULT_VREF,
    };
    esp_err_t ret = adc_cali_create_scheme_line_fitting(&cali_config, &adc_cali_handle);
    if (ret == ESP_OK) {
        LOG_INFO("ADC calibration: line fitting enabled");
        return true;
    }
    if (ret != ESP_ERR_NOT_SUPPORTED) {
        LOG_WARN("ADC calibration: line fitting failed: %s", esp_err_to_name(ret));
    }
#endif

    LOG_INFO("ADC calibration not supported; using approximate scaling");
    return false;
}

#endif

void battery_adcEnable()
{
#ifdef ADC_CTRL
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
#ifdef ADC_CTRL
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

bool battery_adcInit()
{
#if defined(BATTERY_PIN) && defined(ARCH_ESP32)
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = unit,
    };

    if (!adc_handle) {
        esp_err_t err = adc_oneshot_new_unit(&init_config, &adc_handle);
        if (err != ESP_OK) {
            LOG_ERROR("ADC oneshot init failed: %s", esp_err_to_name(err));
            return false;
        }
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = atten,
        .bitwidth = adc_width,
    };

    esp_err_t err = adc_oneshot_config_channel(adc_handle, adc_channel, &chan_cfg);
    if (err != ESP_OK) {
        LOG_ERROR("ADC channel config failed: %s", esp_err_to_name(err));
        return false;
    }

    adc_calibrated = initAdcCalibration();
#endif
    return true;
}

int AnalogBatteryLevel::getBatteryPercent()
{
#if defined(HAS_RAKPROT) && !defined(HAS_PMU)
    if (hasRAK()) {
        return rak9154Sensor.getBusBatteryPercent();
    }
#endif

    float v = getBattVoltage();
    if (v < noBatVolt)
        return -1;

#ifdef NO_BATTERY_LEVEL_ON_CHARGE
    if (v > chargingVolt)
        return 0;
#endif

    float battery_SOC = 0.0;
    uint16_t voltage = v / NUM_CELLS;
    for (int i = 0; i < NUM_OCV_POINTS; i++) {
        if (OCV[i] <= voltage) {
            if (i == 0) {
                battery_SOC = 100.0;
            } else {
                battery_SOC = (float)100.0 / (NUM_OCV_POINTS - 1.0) *
                              (NUM_OCV_POINTS - 1.0 - i + ((float)voltage - OCV[i]) / (OCV[i - 1] - OCV[i]));
            }
            break;
        }
    }
#if defined(BATTERY_CHARGING_INV)
    if (!digitalRead(BATTERY_CHARGING_INV) && battery_SOC > 99)
        battery_SOC = 99;
#endif
    return clamp((int)(battery_SOC), 0, 100);
}

uint16_t AnalogBatteryLevel::getBattVoltage()
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

#ifdef BATTERY_PIN
    float operativeAdcMultiplier =
        config.power.adc_multiplier_override > 0 ? config.power.adc_multiplier_override : ADC_MULTIPLIER;
    const uint32_t min_read_interval = 5000;
    if (!initial_read_done || !Throttle::isWithinTimespanMs(last_read_time_ms, min_read_interval)) {
        last_read_time_ms = millis();

        uint32_t raw = 0;
        float scaled = 0;

        battery_adcEnable();
#ifdef ARCH_STM32WL
        Vref = __LL_ADC_CALC_VREFANALOG_VOLTAGE(analogRead(AVREF), LL_ADC_RESOLUTION);
        raw = analogRead(BATTERY_PIN);
        scaled = __LL_ADC_CALC_DATA_TO_VOLTAGE(Vref, raw, LL_ADC_RESOLUTION);
        scaled *= operativeAdcMultiplier;
#elif defined(ARCH_ESP32)
        raw = espAdcRead();
        int voltage_mv = 0;
        if (adc_calibrated && adc_cali_handle) {
            if (adc_cali_raw_to_voltage(adc_cali_handle, raw, &voltage_mv) != ESP_OK) {
                LOG_WARN("ADC calibration read failed; using raw value");
                voltage_mv = 0;
            }
        }
        if (voltage_mv == 0) {
            const int bits = adcBitWidthToBits(adc_width);
            const float max_code = powf(2.0f, bits) - 1.0f;
            voltage_mv = (int)((raw / max_code) * DEFAULT_VREF);
        }
        scaled = voltage_mv * operativeAdcMultiplier;
#else
#ifdef ARCH_NRF52
        concurrency::LockGuard saadcGuard(concurrency::nrf52SaadcLock);
#endif
        for (uint32_t i = 0; i < BATTERY_SENSE_SAMPLES; i++) {
            raw += analogRead(BATTERY_PIN);
        }
        raw = raw / BATTERY_SENSE_SAMPLES;
        scaled = operativeAdcMultiplier * ((1000 * AREF_VOLTAGE) / pow(2, BATTERY_SENSE_RESOLUTION_BITS)) * raw;
#endif
        battery_adcDisable();

        if (!initial_read_done) {
            if (scaled > last_read_value)
                last_read_value = scaled;
            initial_read_done = true;
        } else {
            last_read_value += (scaled - last_read_value) * 0.5;
        }
    }
    return last_read_value;
#endif
    return 0;
}

#if defined(ARCH_ESP32) && !defined(HAS_PMU) && defined(BATTERY_PIN)
uint32_t AnalogBatteryLevel::espAdcRead()
{
    uint32_t raw = 0;
    uint8_t raw_c = 0;

    if (!adc_handle) {
        LOG_ERROR("ADC oneshot handle not initialized");
        return 0;
    }

    for (int i = 0; i < BATTERY_SENSE_SAMPLES; i++) {
        int val = 0;
        esp_err_t err = adc_oneshot_read(adc_handle, adc_channel, &val);
        if (err == ESP_OK) {
            raw += val;
            raw_c++;
        } else {
            LOG_DEBUG("ADC read failed: %s", esp_err_to_name(err));
        }
    }

    return (raw / (raw_c < 1 ? 1 : raw_c));
}
#endif

bool AnalogBatteryLevel::isBatteryConnect()
{
#ifdef BATTERY_IMMUTABLE
    return true;
#elif defined(ADC_V)
    int lastReading = digitalRead(ADC_V);
    for (int i = 2; i < 500; i++) {
        int reading = digitalRead(ADC_V);
        if (reading != lastReading) {
            return false;
        }
    }
    return true;
#else
    return getBatteryPercent() != -1;
#endif
}

bool AnalogBatteryLevel::isVbusIn()
{
#ifdef EXT_PWR_DETECT
    return digitalRead(EXT_PWR_DETECT) == EXT_PWR_DETECT_VALUE;
#elif defined(MUZI_BASE) || defined(PROMICRO_DIY_TCXO)
    return powerHAL_isVBUSConnected();
#endif
    return getBattVoltage() > chargingVolt;
}

bool AnalogBatteryLevel::isCharging()
{
#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HAS_RAKPROT) && !defined(HAS_PMU)
    if (hasRAK()) {
        return (rak9154Sensor.isCharging()) ? OptTrue : OptFalse;
    }
#endif
#if defined(ELECROW_ThinkNode_M6)
    return digitalRead(EXT_CHRG_DETECT) == EXT_CHRG_DETECT_VALUE || isVbusIn();
#elif defined(EXT_CHRG_DETECT)
    return digitalRead(EXT_CHRG_DETECT) == EXT_CHRG_DETECT_VALUE;
#elif defined(BATTERY_CHARGING_INV)
    return !digitalRead(BATTERY_CHARGING_INV);
#else
#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && !defined(DISABLE_INA_CHARGING_DETECTION)
    if (hasINA()) {
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
    return isVbusIn();
}

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HAS_RAKPROT)
uint16_t AnalogBatteryLevel::getRAKVoltage()
{
    return rak9154Sensor.getBusVoltageMv();
}

bool AnalogBatteryLevel::hasRAK()
{
    if (!rak9154Sensor.isInitialized())
        return rak9154Sensor.runOnce() > 0;
    return rak9154Sensor.isRunning();
}
#endif

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
uint16_t AnalogBatteryLevel::getINAVoltage()
{
    if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA219].first == config.power.device_battery_ina_address) {
        return ina219Sensor.getBusVoltageMv();
    } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA226].first == config.power.device_battery_ina_address) {
        return ina226Sensor.getBusVoltageMv();
    } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA260].first == config.power.device_battery_ina_address) {
        return ina260Sensor.getBusVoltageMv();
    } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA3221].first == config.power.device_battery_ina_address) {
        return ina3221Sensor.getBusVoltageMv();
    }
    return 0;
}

int16_t AnalogBatteryLevel::getINACurrent()
{
    if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA219].first == config.power.device_battery_ina_address) {
        return ina219Sensor.getCurrentMa();
    } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA226].first == config.power.device_battery_ina_address) {
        return ina226Sensor.getCurrentMa();
    } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA3221].first == config.power.device_battery_ina_address) {
        return ina3221Sensor.getCurrentMa();
    }
    return 0;
}

bool AnalogBatteryLevel::hasINA()
{
    if (!config.power.device_battery_ina_address) {
        return false;
    }
    if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA219].first == config.power.device_battery_ina_address) {
        if (!ina219Sensor.isInitialized())
            return ina219Sensor.runOnce() > 0;
        return ina219Sensor.isRunning();
    } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA226].first == config.power.device_battery_ina_address) {
        if (!ina226Sensor.isInitialized())
            return ina226Sensor.runOnce() > 0;
        return ina226Sensor.isRunning();
    } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA260].first == config.power.device_battery_ina_address) {
        if (!ina260Sensor.isInitialized())
            return ina260Sensor.runOnce() > 0;
        return ina260Sensor.isRunning();
    } else if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_INA3221].first == config.power.device_battery_ina_address) {
        if (!ina3221Sensor.isInitialized())
            return ina3221Sensor.runOnce() > 0;
        return ina3221Sensor.isRunning();
    }
    return false;
}
#endif

#if !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_MAX1704X.h>)
bool MAX17048BatteryLevel::runOnce()
{
    if (max17048 == nullptr) {
        max17048 = MAX17048Singleton::GetInstance();
    }

    if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_MAX17048].first != 0) {
        return max17048->runOnce(nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_MAX17048].second);
    }
    return false;
}

int MAX17048BatteryLevel::getBatteryPercent()
{
    return max17048->getBusBatteryPercent();
}

uint16_t MAX17048BatteryLevel::getBattVoltage()
{
    return max17048->getBusVoltageMv();
}

bool MAX17048BatteryLevel::isBatteryConnect()
{
    return max17048->isBatteryConnected();
}

bool MAX17048BatteryLevel::isVbusIn()
{
    return max17048->isExternallyPowered();
}

bool MAX17048BatteryLevel::isCharging()
{
    return max17048->isBatteryCharging();
}
#endif

#if !MESHTASTIC_EXCLUDE_I2C && HAS_CW2015
int CW2015BatteryLevel::getBatteryPercent()
{
    int data = -1;
    Wire.beginTransmission(CW2015_ADDR);
    Wire.write(0x04);
    if (Wire.endTransmission() == 0) {
        if (Wire.requestFrom(CW2015_ADDR, (uint8_t)1)) {
            data = Wire.read();
        }
    }
    return data;
}

uint16_t CW2015BatteryLevel::getBattVoltage()
{
    uint16_t mv = 0;
    Wire.beginTransmission(CW2015_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission() == 0) {
        if (Wire.requestFrom(CW2015_ADDR, (uint8_t)2)) {
            mv = Wire.read();
            mv <<= 8;
            mv |= Wire.read();
            mv = mv * 305 / 1000;
        }
    }
    return mv;
}
#endif

#if defined(HAS_PPM) && HAS_PPM
extern XPowersPPM *PPM;

bool LipoCharger::runOnce()
{
    if (PPM == nullptr) {
        PPM = new XPowersPPM;
        bool result = PPM->init(Wire, I2C_SDA, I2C_SCL, BQ25896_ADDR);
        if (result) {
            LOG_INFO("PPM BQ25896 init succeeded");
            PPM->setChargeTargetVoltage(4288);
            PPM->setChargerConstantCurr(1024);
            PPM->enableMeasure();
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
        }

        LOG_WARN("BQ27220 init failed");
        delete bq;
        bq = nullptr;
        return false;
    }
    return false;
}

int LipoCharger::getBatteryPercent()
{
    return -1;
}

uint16_t LipoCharger::getBattVoltage()
{
    return bq->getVoltage();
}

bool LipoCharger::isBatteryConnect()
{
    return PPM->getBattVoltage() > 0;
}

bool LipoCharger::isVbusIn()
{
    return PPM->isVbusIn();
}

bool LipoCharger::isCharging()
{
    bool charging = PPM->isCharging();
    if (charging) {
        LOG_DEBUG("BQ27220 time to full charge: %d min", bq->getTimeToFull());
    } else if (!PPM->isVbusIn()) {
        LOG_DEBUG("BQ27220 time to empty: %d min (%d mAh)", bq->getTimeToEmpty(), bq->getRemainingCapacity());
    }
    return charging;
}
#endif

#ifdef HELTEC_MESH_SOLAR
bool meshSolarBatteryLevel::runOnce()
{
    meshSolarStart();
    return true;
}

int meshSolarBatteryLevel::getBatteryPercent()
{
    return meshSolarGetBatteryPercent();
}

uint16_t meshSolarBatteryLevel::getBattVoltage()
{
    return meshSolarGetBattVoltage();
}

bool meshSolarBatteryLevel::isBatteryConnect()
{
    return meshSolarIsBatteryConnect();
}

bool meshSolarBatteryLevel::isVbusIn()
{
    return meshSolarIsVbusIn();
}

bool meshSolarBatteryLevel::isCharging()
{
    return meshSolarIsCharging();
}
#endif

#ifdef HAS_SERIAL_BATTERY_LEVEL
bool SerialBatteryLevel::runOnce()
{
    BatterySerial.begin(4800);
    return true;
}

int SerialBatteryLevel::getBatteryPercent()
{
    return v_percent;
}

uint16_t SerialBatteryLevel::getBattVoltage()
{
    return voltage * 1000;
}

bool SerialBatteryLevel::isBatteryConnect()
{
    if (BatterySerial.available() > 5) {
        while (BatterySerial.available() > 11) {
            BatterySerial.read();
        }
        int tries = 0;
        while (BatterySerial.read() != 0xFE) {
            tries++;
            if (tries > 10) {
                LOG_WARN("SerialBatteryLevel: no start byte found");
                return true;
            }
        }

        Data[1] = BatterySerial.read();
        Data[2] = BatterySerial.read();
        Data[3] = BatterySerial.read();
        Data[4] = BatterySerial.read();
        Data[5] = BatterySerial.read();
        if (Data[5] != 0xFD) {
            LOG_WARN("SerialBatteryLevel: invalid end byte %02x", Data[5]);
            return true;
        }
        v_percent = Data[1];
        voltage = Data[2] + (((float)Data[3]) / 100) + (((float)Data[4]) / 10000);
        voltage *= 2;
        return true;
    }
    return true;
}

bool SerialBatteryLevel::isVbusIn()
{
#ifdef EXT_CHRG_DETECT
    return digitalRead(EXT_CHRG_DETECT) == EXT_CHRG_DETECT_VALUE;
#endif
    return false;
}

bool SerialBatteryLevel::isCharging()
{
#ifdef EXT_CHRG_DETECT
    return digitalRead(EXT_CHRG_DETECT) == EXT_CHRG_DETECT_VALUE;
#endif
    return isVbusIn();
}
#endif