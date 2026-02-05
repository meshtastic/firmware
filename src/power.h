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
#define OCV_ARRAY 4190, 4050, 3990, 3890, 3800, 3720, 3630, 3530, 3420, 3300, 3100
#endif

/*Note: 12V lead acid is 6 cells, most board accept only 1 cell LiIon/LiPo*/
#ifndef NUM_CELLS
#define NUM_CELLS 1
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

  protected:
    meshtastic::PowerStatus *statusHandler;

    /// Setup a xpowers chip axp192/axp2101, return true if found
    bool axpChipInit();
    /// Setup a simple ADC input based battery sensor
    bool analogInit();
    /// Setup a Lipo battery level sensor
    bool lipoInit();
    /// Setup a Lipo charger
    bool lipoChargerInit();
    /// Setup a meshSolar battery sensor
    bool meshSolarInit();
    /// Setup a serial battery sensor
    bool serialBatteryInit();

  private:
    void shutdown();
    void reboot();
    // open circuit voltage lookup table
    uint8_t low_voltage_counter;
    uint32_t lastLogTime = 0;
#ifdef DEBUG_HEAP
    uint32_t lastheap;
#endif
};

void battery_adcEnable();

extern Power *power;
