#pragma once
#include "PowerStatus.h"
#include "concurrency/OSThread.h"
#include "configuration.h"

#ifdef ARCH_ESP32
// "legacy adc calibration driver is deprecated, please migrate to use esp_adc/adc_cali.h and esp_adc/adc_cali_scheme.h
#include <esp_adc_cal.h>
#include <soc/adc_channel.h>
#endif

#ifndef NUM_OCV_POINTS
#define NUM_OCV_POINTS 11
#endif

// Device specific curves go in variant.h
#ifndef OCV_ARRAY
#if defined(ARCH_STM32WL) && BATTERY_PIN == AVBAT
// STM32 VDD/VBAT absolute maximum is 4V so use an LFP curve
#define OCV_ARRAY 3650, 3400, 3340, 3320, 3300, 3280, 3270, 3260, 3240, 3200, 2500
#else
#define OCV_ARRAY 4190, 4050, 3990, 3890, 3800, 3720, 3630, 3530, 3420, 3300, 3100
#endif
#endif

/*Note: 12V lead acid is 6 cells, most board accept only 1 cell LiIon/LiPo*/
#ifndef NUM_CELLS
#define NUM_CELLS 1
#endif

// Set the number of samples, it has an effect of increasing sensitivity in complex electromagnetic environment.
#ifndef BATTERY_SENSE_SAMPLES
#define BATTERY_SENSE_SAMPLES 15
#endif

#ifndef ADC_MULTIPLIER
#define ADC_MULTIPLIER 2.0
#endif

#ifdef EXT_PWR_DETECT
#ifndef EXT_PWR_DETECT_MODE
#define EXT_PWR_DETECT_MODE INPUT
// If using internal pull resistors, we can infer EXT_PWR_DETECT_VALUE
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
// If using internal pull resistors, we can infer EXT_CHRG_DETECT_VALUE
#elif EXT_CHRG_DETECT_MODE == INPUT_PULLUP
#define EXT_CHRG_DETECT_VALUE LOW
#elif EXT_CHRG_DETECT_MODE == INPUT_PULLDOWN
#define EXT_CHRG_DETECT_VALUE HIGH
#endif
#ifndef EXT_CHRG_DETECT_VALUE
#define EXT_CHRG_DETECT_VALUE HIGH
#endif
#endif

#ifndef DELAY_FOREVER
#define DELAY_FOREVER portMAX_DELAY
#endif

// NRF52 has AREF_VOLTAGE defined in architecture.h but
// make sure it's included. If something is wrong with NRF52
// definition - compilation will fail on missing definition
#if !defined(AREF_VOLTAGE) && !defined(ARCH_NRF52)
#define AREF_VOLTAGE 3.3
#endif

#ifdef BAT_MEASURE_ADC_UNIT
extern RTC_NOINIT_ATTR uint64_t RTC_reg_b;
#include "soc/sens_reg.h" // needed for adc pin reset
#endif

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
#include "modules/Telemetry/Sensor/nullSensor.h"
#if __has_include(<Adafruit_INA219.h>)
#include "modules/Telemetry/Sensor/INA219Sensor.h"
extern INA219Sensor ina219Sensor;
#else
extern NullSensor ina219Sensor;
#endif

#if __has_include(<INA226.h>)
#include "modules/Telemetry/Sensor/INA226Sensor.h"
extern INA226Sensor ina226Sensor;
#else
extern NullSensor ina226Sensor;
#endif

#if __has_include(<Adafruit_INA260.h>)
#include "modules/Telemetry/Sensor/INA260Sensor.h"
extern INA260Sensor ina260Sensor;
#else
extern NullSensor ina260Sensor;
#endif

#if __has_include(<INA3221.h>)
#include "modules/Telemetry/Sensor/INA3221Sensor.h"
extern INA3221Sensor ina3221Sensor;
#else
extern NullSensor ina3221Sensor;
#endif

#endif

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
#if __has_include(<Adafruit_MAX1704X.h>)
#include "modules/Telemetry/Sensor/MAX17048Sensor.h"
extern MAX17048Sensor max17048Sensor;
#else
extern NullSensor max17048Sensor;
#endif
#endif

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && HAS_RAKPROT
#include "modules/Telemetry/Sensor/RAK9154Sensor.h"
extern RAK9154Sensor rak9154Sensor;
#endif

#ifdef HAS_PMU
#include "XPowersAXP192.tpp"
#include "XPowersAXP2101.tpp"
#include "XPowersLibInterface.hpp"
extern XPowersLibInterface *PMU;
#endif

#ifndef HAS_PMU
// Copy of the base class defined in axp20x.h, to prevent an automatic wire.h dependency
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

class Power : private concurrency::OSThread
{

  public:
    Observable<const meshtastic::PowerStatus *> newStatus;

    Power();

    void powerCommandsCheck();
    void readPowerStatus();
    virtual bool setup();
    virtual int32_t runOnce() override;
    void setStatusHandler(meshtastic::PowerStatus *handler) { statusHandler = handler; }
    const uint16_t OCV[11] = {OCV_ARRAY};

#ifdef ARCH_ESP32
    int beforeLightSleep(void *unused);
    int afterLightSleep(esp_sleep_wakeup_cause_t cause);
#endif

    void attachPowerInterrupts();
    void detachPowerInterrupts();

  protected:
    meshtastic::PowerStatus *statusHandler;

    /// Setup a xpowers chip axp192/axp2101, return true if found
    bool axpChipInit();
    /// Setup a simple ADC input based battery sensor
    bool analogInit();
    /// Setup cw2015 battery level sensor
    bool cw2015Init();
    /// Setup a 17048 battery level sensor
    bool max17048Init();
    /// Setup a Lipo charger
    bool lipoChargerInit();
    /// Setup a meshSolar battery sensor
    bool meshSolarInit();
    /// Setup a serial battery sensor
    bool serialBatteryInit();

    bool pmu_irq = false;

  private:
    void shutdown();
    void reboot();
    // open circuit voltage lookup table
    uint8_t low_voltage_counter;
    uint32_t lastLogTime = 0;

#ifdef ARCH_ESP32
    // Get notified when lightsleep begins and ends
    CallbackObserver<Power, void *> lsObserver = CallbackObserver<Power, void *>(this, &Power::beforeLightSleep);
    CallbackObserver<Power, esp_sleep_wakeup_cause_t> lsEndObserver =
        CallbackObserver<Power, esp_sleep_wakeup_cause_t>(this, &Power::afterLightSleep);
#endif

#ifdef DEBUG_HEAP
    uint32_t lastheap;
#endif
};

void battery_adcEnable();

extern Power *power;
