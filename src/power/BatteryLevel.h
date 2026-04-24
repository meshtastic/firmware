#pragma once

#include "power.h"

#ifdef HAS_SERIAL_BATTERY_LEVEL
#include <SoftwareSerial.h>
#endif

#if defined(ARCH_STM32WL) && defined(BATTERY_PIN)
#include "stm32yyxx_ll_adc.h"

/* Analog read resolution */
#if defined(LL_ADC_RESOLUTION_12B)
#define LL_ADC_RESOLUTION LL_ADC_RESOLUTION_12B
#define BATTERY_SENSE_RESOLUTION_BITS 12
#elif defined(LL_ADC_DS_DATA_WIDTH_12_BIT)
#define LL_ADC_RESOLUTION LL_ADC_DS_DATA_WIDTH_12_BIT
#define BATTERY_SENSE_RESOLUTION_BITS 12
#else
#error "ADC resolution could not be defined!"
#endif
#define ADC_RANGE (1 << BATTERY_SENSE_RESOLUTION_BITS)
#endif

#ifdef EXT_PWR_DETECT
#ifndef EXT_PWR_DETECT_MODE
#define EXT_PWR_DETECT_MODE INPUT
#elif EXT_PWR_DETECT_MODE == INPUT_PULLUP
#define EXT_PWR_DETECT_VALUE LOW
#elif EXT_PWR_DETECT_MODE == INPUT_PULLDOWN
#define EXT_PWR_DETECT_VALUE HIGH
#endif
#ifndef EXT_PWR_DETECT_VALUE
#define EXT_PWR_DETECT_VALUE HIGH
#endif
#endif

#ifdef EXT_CHRG_DETECT
#ifndef EXT_CHRG_DETECT_MODE
#define EXT_CHRG_DETECT_MODE INPUT
#elif EXT_CHRG_DETECT_MODE == INPUT_PULLUP
#define EXT_CHRG_DETECT_VALUE LOW
#elif EXT_CHRG_DETECT_MODE == INPUT_PULLDOWN
#define EXT_CHRG_DETECT_VALUE HIGH
#endif
#ifndef EXT_CHRG_DETECT_VALUE
#define EXT_CHRG_DETECT_VALUE HIGH
#endif
#endif

#ifndef HAS_PMU
class HasBatteryLevel
{
  public:
    virtual int getBatteryPercent() { return -1; }
    virtual uint16_t getBattVoltage() { return 0; }
    virtual bool isBatteryConnect() { return false; }
    virtual bool isVbusIn() { return false; }
    virtual bool isCharging() { return false; }
};
#endif

class AnalogBatteryLevel : public HasBatteryLevel
{
  public:
    int getBatteryPercent() override;
    uint16_t getBattVoltage() override;
    bool isBatteryConnect() override;
    bool isVbusIn() override;
    bool isCharging() override;

#if defined(ARCH_ESP32) && !defined(HAS_PMU) && defined(BATTERY_PIN)
    uint32_t espAdcRead();
#endif

  private:
    const uint16_t OCV[NUM_OCV_POINTS] = {OCV_ARRAY};
    const float chargingVolt = (OCV[0] + 10) * NUM_CELLS;
    const float noBatVolt = (OCV[NUM_OCV_POINTS - 1] - 500) * NUM_CELLS;
    bool initial_read_done = false;
    float last_read_value = (OCV[NUM_OCV_POINTS - 1] * NUM_CELLS);
    uint32_t last_read_time_ms = 0;

#ifdef ARCH_STM32WL
    uint32_t Vref = 3300;
#endif

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HAS_RAKPROT)
    uint16_t getRAKVoltage();
    bool hasRAK();
#endif

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
    uint16_t getINAVoltage();
    int16_t getINACurrent();
    bool hasINA();
#endif
};

#if !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_MAX1704X.h>)
class MAX17048BatteryLevel : public HasBatteryLevel
{
  private:
    MAX17048Singleton *max17048 = nullptr;

  public:
    bool runOnce();
    int getBatteryPercent() override;
    uint16_t getBattVoltage() override;
    bool isBatteryConnect() override;
    bool isVbusIn() override;
    bool isCharging() override;
};
#endif

#if !MESHTASTIC_EXCLUDE_I2C && HAS_CW2015
class CW2015BatteryLevel : public AnalogBatteryLevel
{
  public:
    int getBatteryPercent() override;
    uint16_t getBattVoltage() override;
};
#endif

#if defined(HAS_PPM) && HAS_PPM
class BQ27220;

class LipoCharger : public HasBatteryLevel
{
  private:
    BQ27220 *bq = nullptr;

  public:
    bool runOnce();
    int getBatteryPercent() override;
    uint16_t getBattVoltage() override;
    bool isBatteryConnect() override;
    bool isVbusIn() override;
    bool isCharging() override;
};
#endif

#ifdef HELTEC_MESH_SOLAR
class meshSolarBatteryLevel : public HasBatteryLevel
{
  public:
    bool runOnce();
    int getBatteryPercent() override;
    uint16_t getBattVoltage() override;
    bool isBatteryConnect() override;
    bool isVbusIn() override;
    bool isCharging() override;
};
#endif

#ifdef HAS_SERIAL_BATTERY_LEVEL
class SerialBatteryLevel : public HasBatteryLevel
{
  public:
    bool runOnce();
    int getBatteryPercent() override;
    uint16_t getBattVoltage() override;
    bool isBatteryConnect() override;
    bool isVbusIn() override;
    bool isCharging() override;

  private:
    SoftwareSerial BatterySerial = SoftwareSerial(SERIAL_BATTERY_RX, SERIAL_BATTERY_TX);
    uint8_t Data[6] = {0};
    int v_percent = 0;
    float voltage = 0.0;
};
#endif

bool battery_adcInit();